#include "acp_jni_registry.h"
#include <stddef.h>
#include <string.h>

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(struct JNINativeInterface, RegisterNatives) == 0x35c, "JNI ARM registration offset");
_Static_assert(offsetof(struct JNINativeInterface, GetJavaVM) == 0x36c, "JNI ARM VM offset");
_Static_assert(sizeof(JNINativeMethod) == 12, "JNI ARM native table layout");
#endif
static void lock(acp_jni_registry *r) { while (atomic_flag_test_and_set_explicit(&r->lock, memory_order_acquire)) {} }
static void unlock(acp_jni_registry *r) { atomic_flag_clear_explicit(&r->lock, memory_order_release); }
static size_t bounded_len(const char *s, size_t max) {
    if (!s) return max;
    size_t i = 0; while (i < max && s[i]) i++;
    return i;
}
static int text_ok(const char *s, size_t max) { size_t n = bounded_len(s,max); return n && n < max; }
/* Return one field descriptor's end. A method parameter cannot be void. */
static const char *type_end(const char *s, int void_ok, unsigned *slots) {
    unsigned arrays = 0;
    while (*s == '[') { arrays++; s++; if (arrays > 255) return NULL; }
    char c = *s++;
    if (!c) return NULL;
    if (c == 'V') return void_ok && !arrays ? s : NULL;
    if (c == 'L') {
        const char *start = s;
        int segment_start = 1;
        while (*s && *s != ';') {
            if ((unsigned char)*s <= 32 || *s == '.' || *s == '[' || *s == '(' || *s == ')' || *s == '<' || *s == '>') return NULL;
            if (*s == '/') { if (segment_start) return NULL; segment_start = 1; }
            else segment_start = 0;
            s++;
        }
        if (s == start || segment_start || *s != ';') return NULL;
        s++;
    } else if (!strchr("ZBCSIJFD", c)) return NULL;
    if (slots) *slots += !arrays && (c == 'J' || c == 'D') ? 2u : 1u;
    return s;
}
int acp_jni_valid_signature(const char *s) {
    if (!text_ok(s, ACP_JNI_SIGNATURE) || *s++ != '(') return 0;
    unsigned slots = 0;
    while (*s && *s != ')') { s = type_end(s,0,&slots); if (!s || slots > 255) return 0; }
    if (*s++ != ')') return 0;
    s = type_end(s,1,NULL);
    return s && !*s;
}
static int class_index(acp_jni_registry *r, const char *name) {
    for (unsigned i = 0; i < r->class_count; i++) if (!strcmp(name,r->classes[i])) return (int)i;
    return -1;
}
static int method_index(acp_jni_registry *r, int cls, const char *name, const char *sig) {
    if (cls < 0) return -1;
    for (unsigned i = 0; i < r->method_count; i++) {
        acp_jni_native *m = &r->methods[i];
        if (m->class_index == (unsigned)cls && !strcmp(name,m->name) && !strcmp(sig,m->signature)) return (int)i;
    }
    return -1;
}
void acp_jni_registry_init(acp_jni_registry *r) { memset(r,0,sizeof(*r)); atomic_flag_clear(&r->lock); }
int acp_jni_declare(acp_jni_registry *r, const char *cls, const char *name, const char *sig) {
    if (!r || !text_ok(cls,ACP_JNI_CLASS_NAME) || !text_ok(name,ACP_JNI_METHOD_NAME) || !acp_jni_valid_signature(sig)) return ACP_JNI_INVALID;
    lock(r);
    int c = class_index(r,cls), status = ACP_JNI_OK;
    if (method_index(r,c,name,sig) >= 0) goto done;
    if (r->method_count == ACP_JNI_METHODS || (c < 0 && r->class_count == ACP_JNI_CLASSES)) {
        status = ACP_JNI_CAPACITY; goto done;
    }
    if (c < 0) { c = (int)r->class_count++; strcpy(r->classes[c],cls); }
    acp_jni_native *m = &r->methods[r->method_count++];
    strcpy(m->name,name); strcpy(m->signature,sig); m->class_index = (unsigned)c;
done:
    unlock(r); return status;
}
int acp_jni_register(acp_jni_registry *r, const char *cls, const JNINativeMethod *methods,
                    jint count, acp_jni_code_check check, void *ctx) {
    if (!r || !text_ok(cls,ACP_JNI_CLASS_NAME) || count < 0 || count > ACP_JNI_METHODS ||
        (count && (!methods || !check))) return ACP_JNI_INVALID;
    int indices[ACP_JNI_METHODS], status = ACP_JNI_OK;
    lock(r);
    int c = class_index(r,cls);
    if (c < 0) { status = ACP_JNI_UNKNOWN; goto done; }
    for (jint i = 0; i < count; i++) {
        const JNINativeMethod *m = &methods[i];
        if (!text_ok(m->name,ACP_JNI_METHOD_NAME) || !acp_jni_valid_signature(m->signature) || !m->fnPtr) {
            status = ACP_JNI_INVALID; goto done;
        }
        indices[i] = method_index(r,c,m->name,m->signature);
        if (indices[i] < 0) { status = ACP_JNI_UNKNOWN; goto done; }
        for (jint j = 0; j < i; j++) if (indices[i] == indices[j]) { status = ACP_JNI_INVALID; goto done; }
        /* This check must be a pure range check, never a callback into registry. */
        if (!check(ctx,m->fnPtr)) { status = ACP_JNI_ADDRESS; goto done; }
    }
    for (jint i = 0; i < count; i++) r->methods[indices[i]].function = methods[i].fnPtr;
done:
    unlock(r); return status;
}
int acp_jni_unregister(acp_jni_registry *r, const char *cls) {
    if (!r || !text_ok(cls,ACP_JNI_CLASS_NAME)) return ACP_JNI_INVALID;
    lock(r);
    int c = class_index(r,cls);
    if (c >= 0) for (unsigned i = 0; i < r->method_count; i++)
        if (r->methods[i].class_index == (unsigned)c) r->methods[i].function = NULL;
    unlock(r);
    return c < 0 ? ACP_JNI_UNKNOWN : ACP_JNI_OK;
}
void *acp_jni_resolve(acp_jni_registry *r, const char *cls, const char *name, const char *sig) {
    if (!r || !text_ok(cls,ACP_JNI_CLASS_NAME) || !text_ok(name,ACP_JNI_METHOD_NAME) || !acp_jni_valid_signature(sig)) return NULL;
    lock(r);
    int i = method_index(r,class_index(r,cls),name,sig);
    void *p = i < 0 ? NULL : r->methods[i].function;
    unlock(r); return p;
}
int acp_jni_snapshot(acp_jni_registry *r, const char *cls, const JNINativeMethod *req, jint count, void **out) {
    if (!r || !text_ok(cls,ACP_JNI_CLASS_NAME) || count < 0 || count > ACP_JNI_METHODS || (count && (!req || !out))) return ACP_JNI_INVALID;
    int indices[ACP_JNI_METHODS], status = ACP_JNI_OK;
    lock(r);
    int c = class_index(r,cls);
    if (c < 0) { status = ACP_JNI_UNKNOWN; goto done; }
    for (jint i = 0; i < count; i++) {
        if (!text_ok(req[i].name,ACP_JNI_METHOD_NAME) || !acp_jni_valid_signature(req[i].signature)) {
            status = ACP_JNI_INVALID; goto done;
        }
        indices[i] = method_index(r,c,req[i].name,req[i].signature);
        if (indices[i] < 0 || !r->methods[indices[i]].function) { status = ACP_JNI_UNKNOWN; goto done; }
    }
    for (jint i = 0; i < count; i++) out[i] = r->methods[indices[i]].function;
done:
    unlock(r); return status;
}
