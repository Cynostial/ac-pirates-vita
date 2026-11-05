#include "acp_jni_runtime.h"
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JR_THREADS 16u
#define JR_CLASSES 128u
#define JR_OBJECTS 256u
#define JR_REFS 1024u
#define JR_FRAMES 16u
#define JR_PINS 256u
#define JR_METHODS 384u
#define JR_FIELDS 128u
#define JR_FIELD_CELLS 512u
#define JR_ARGS 32u
#define JR_BLOCK 256u
#define JR_BLOCKS 1024u
#define JR_INDEX_BITS 11u
#define JR_INDEX_MASK ((1u << JR_INDEX_BITS) - 1u)
#define JR_GENERATION_MAX (UINT32_MAX >> JR_INDEX_BITS)
enum { K_FREE, K_CLASS, K_OBJECT, K_STRING, K_BUFFER, K_EXCEPTION, K_PRIMITIVE_ARRAY, K_OBJECT_ARRAY, K_REFLECT_METHOD, K_REFLECT_FIELD };
enum { C_OBJECT, C_CLASS, C_STRING, C_THROWABLE, C_ERROR, C_EXCEPTION,
       C_RUNTIME, C_OOM, C_NOCLASS, C_NOMETHOD, C_ARGUMENT, C_BOUNDS,
       C_STATE, C_NULL, C_BUFFER, C_BYTEBUFFER, C_LINKAGE, C_INCOMPATIBLE, C_INDEX,
       C_NEGATIVE_ARRAY, C_ARRAY_BOUNDS, C_ARRAY_STORE, C_NOFIELD, C_MONITOR };
typedef struct { unsigned quota, used; } jr_frame;
typedef struct {
    JNIEnv env; /* JNI ABI requires interface pointer first. */
    struct acp_jni_runtime *r;
    uintptr_t id;
    unsigned index, depth, pending, pins, calls;
    char exception_context[512];
    unsigned exception_reported;
    unsigned call_frames[JR_FRAMES];
    jr_frame frames[JR_FRAMES];
} jr_thread;
typedef struct {
    unsigned kind, cls, refs, pins, block, blocks;
    unsigned monitor_owner,monitor_depth,reference_fields;
    jsize length, utf_length;
    void *data;
    jlong capacity;
} jr_object;
typedef struct {
    unsigned object, generation, owner, frame, kind;
} jr_ref;
typedef struct { char name[ACP_JNI_CLASS_NAME]; int parent; unsigned object; } jr_class;
typedef struct { unsigned object, owner, utf, count; } jr_pin;
typedef struct {
    char name[ACP_JNI_METHOD_NAME], signature[ACP_JNI_SIGNATURE];
    unsigned cls, is_static, argc, result_offset;
    unsigned short arg_offset[JR_ARGS];
    acp_jni_method_fn call;
    void *ctx;
} jr_method;
typedef struct {char name[ACP_JNI_METHOD_NAME],signature[ACP_JNI_SIGNATURE];unsigned cls,is_static;jvalue value;} jr_field;
typedef struct {unsigned object,field;jvalue value;} jr_field_cell;
struct acp_jni_runtime {
    JavaVM vm; /* invocation table pointer first */
    acp_jni_ops ops;
    acp_jni_monitor_ops monitors;
    void (*exception_report)(void *,const char *);
    void *exception_report_ctx;
    unsigned trace_exceptions;
    acp_jni_registry *registry;
    unsigned classes, live_threads, free_refs, reservations, payload_used, payload_peak;
    unsigned oom_object;
    unsigned methods, sealed, object_arrays,fields,field_cells;
    jr_method method[JR_METHODS];
    jr_field field[JR_FIELDS];jr_field_cell cells[JR_FIELD_CELLS];
    jr_thread threads[JR_THREADS];
    jr_class types[JR_CLASSES];
    jr_object objects[JR_OBJECTS];
    jr_ref refs[JR_REFS];
    jr_pin pins[JR_PINS];
    unsigned char blocks[JR_BLOCKS];
    _Alignas(8) unsigned char payload[JR_BLOCK * JR_BLOCKS];
};
static const struct JNINativeInterface jr_table;
static const struct JNIInvokeInterface jr_vm_table;
_Static_assert(offsetof(jr_thread,env) == 0, "JNIEnv must lead thread state");
_Static_assert(offsetof(struct acp_jni_runtime,vm) == 0, "JavaVM must lead runtime");

static _Noreturn void fail(acp_jni_runtime *r, const char *reason) {
    r->ops.fatal(r->ops.ctx,reason);
    abort();
}
static jr_thread *enter(JNIEnv *env) {
    jr_thread *t = (jr_thread *)env;
    acp_jni_runtime *r = t->r;
    r->ops.lock(r->ops.ctx);
    if (!t->id || t->id != r->ops.thread_id(r->ops.ctx)) {
        r->ops.unlock(r->ops.ctx);
        fail(r,"JNIEnv used by a detached or different thread");
    }
    return t;
}
static void leave(jr_thread *t) {
    acp_jni_runtime *r=t->r;
    void (*report)(void *,const char *)=NULL;char message[768];void *ctx=NULL;
    if(r->trace_exceptions&&r->exception_report&&t->pending&&!t->exception_reported){
        t->exception_reported=1;report=r->exception_report;ctx=r->exception_report_ctx;
        snprintf(message,sizeof(message),"%s%s%s",r->types[r->objects[t->pending-1].cls].name,
                 *t->exception_context?": ":"",t->exception_context);
    }
    r->ops.unlock(r->ops.ctx);
    if(report)report(ctx,message);
}
static _Noreturn void unsupported(JNIEnv *env, const char *name) {
    jr_thread *t = enter(env);
    leave(t);
    fail(t->r,name);
}
static int class_index(acp_jni_runtime *r, const char *name) {
    if (!name) return -1;
    for (unsigned i=0; i<r->classes; i++) if (!strcmp(r->types[i].name,name)) return (int)i;
    return -1;
}
static int add_class(acp_jni_runtime *,const char *,int);
static int resolve_class(acp_jni_runtime *r,const char *name) {
    int found=class_index(r,name);if(found>=0||!name||name[0]!='[')return found;
    size_t size=strlen(name);if(size<3||size>=ACP_JNI_CLASS_NAME)return -1;
    int component=-1;
    if(name[1]=='[')component=resolve_class(r,name+1);
    else if(name[1]=='L'&&name[size-1]==';') {
        char child[ACP_JNI_CLASS_NAME];memcpy(child,name+2,size-3);child[size-3]=0;component=class_index(r,child);
    }
    if(component<0||add_class(r,name,C_OBJECT)!=JNI_OK)return -1;
    return class_index(r,name);
}
static int array_component(acp_jni_runtime *r,const char *descriptor) {
    if(descriptor[0]!='[')return -1;
    if(descriptor[1]=='[')return class_index(r,descriptor+1);
    if(descriptor[1]!='L')return -1;
    size_t n=strlen(descriptor+2);if(!n||descriptor[n+1]!=';'||n>=ACP_JNI_CLASS_NAME)return -1;
    char name[ACP_JNI_CLASS_NAME];memcpy(name,descriptor+2,n-1);name[n-1]=0;return class_index(r,name);
}
static int assignable(acp_jni_runtime *r, unsigned actual, unsigned wanted) {
    if(actual==wanted)return 1;
    if(r->types[actual].name[0]=='[') {
        const char *name=r->types[wanted].name;
        if(!strcmp(name,"java/lang/Cloneable")||!strcmp(name,"java/io/Serializable"))return 1;
        int ac=array_component(r,r->types[actual].name),wc=array_component(r,name);
        if(ac>=0&&wc>=0)return assignable(r,(unsigned)ac,(unsigned)wc);
    }
    for (int c=(int)actual; c>=0; c=r->types[c].parent) if ((unsigned)c==wanted) return 1;
    return 0;
}
static unsigned object_new(acp_jni_runtime *r, unsigned kind, unsigned cls) {
    for (unsigned i=0; i<JR_OBJECTS; i++) if (!r->objects[i].kind) {
        r->objects[i]=(jr_object){.kind=kind,.cls=cls};
        return i+1;
    }
    return 0;
}
static void object_drop(acp_jni_runtime *r, unsigned id);
static void weak_clear(acp_jni_runtime *r,unsigned id){for(unsigned i=0;i<JR_REFS;i++)if(r->refs[i].kind==JNIWeakGlobalRefType&&r->refs[i].object==id)r->refs[i].object=0;}
static int field_is_reference(const jr_field *f){return f->signature[0]=='L'||f->signature[0]=='[';}
static int object_has_links(acp_jni_runtime *r,unsigned id){
    if(!id)return 0;
    jr_object *o=&r->objects[id-1];
    return o->reference_fields||(o->kind==K_OBJECT_ARRAY&&o->length)||
           (o->kind==K_EXCEPTION&&o->capacity);
}
static void object_collect(acp_jni_runtime *r, unsigned id) {
    jr_object *o=&r->objects[id-1];
    if (o->refs || o->pins) return;
    weak_clear(r,id);
    for(unsigned i=0;i<JR_FIELD_CELLS;i++)if(r->cells[i].object==id) {
        jr_field_cell *cell=&r->cells[i];unsigned child=field_is_reference(&r->field[cell->field])?(unsigned)(uintptr_t)cell->value.l:0;
        memset(cell,0,sizeof(*cell));r->field_cells--;object_drop(r,child);
    }
    if(o->kind==K_OBJECT_ARRAY) {
        /* Break the outgoing links before recursively releasing their owners. */
        uint32_t *items=o->data;jsize count=o->length;o->length=0;r->object_arrays--;
        for(jsize i=0;i<count;i++){unsigned child=items[i];items[i]=0;object_drop(r,child);}
    }
    if (o->blocks) {
        memset(r->blocks+o->block,0,o->blocks);
        r->payload_used -= o->blocks*JR_BLOCK;
    }
    unsigned message=o->kind==K_EXCEPTION?(unsigned)o->capacity:0;
    memset(o,0,sizeof(*o));
    /* Throwable holds a strong String reference; strings cannot refer back. */
    object_drop(r,message);
}
static void object_drop(acp_jni_runtime *r, unsigned id) {
    if (!id) return;
    r->objects[id-1].refs--;
    object_collect(r,id);
}
static void collect_array_cycles(acp_jni_runtime *r) {
    if(!r->object_arrays&&!r->field_cells)return;
    /* Refcounts include external JNI roots and internal array/message links.
     * Subtract only the latter to identify roots, preserving temporary roots
     * held while PopLocalFrame transfers a result. No allocation is required. */
    unsigned incoming[JR_OBJECTS]={0},stack[JR_OBJECTS],top=0;unsigned char marked[JR_OBJECTS]={0};
    /* Build field adjacency once. Scanning all 512 field slots for every
     * reachable object made each local-reference release disproportionately
     * expensive during the game's thousands of manifest string operations. */
    unsigned short heads[JR_OBJECTS]={0},next[JR_FIELD_CELLS];
    for(unsigned i=0;i<JR_OBJECTS;i++) {
        jr_object *o=&r->objects[i];
        if(o->kind==K_OBJECT_ARRAY)for(jsize j=0;j<o->length;j++){uint32_t id=((uint32_t*)o->data)[j];if(id)incoming[id-1]++;}
        else if(o->kind==K_EXCEPTION&&o->capacity)incoming[(unsigned)o->capacity-1]++;
    }
    for(unsigned i=0;i<JR_FIELD_CELLS;i++) {
        jr_field_cell *c=&r->cells[i];
        if(c->object&&field_is_reference(&r->field[c->field])&&c->value.l){
            incoming[(unsigned)(uintptr_t)c->value.l-1]++;
            next[i]=heads[c->object-1];heads[c->object-1]=(unsigned short)(i+1);
        }
    }
    for(unsigned i=0;i<JR_OBJECTS;i++)if(r->objects[i].kind&&(r->objects[i].refs>incoming[i]||r->objects[i].pins)) {
        marked[i]=1;stack[top++]=i;
    }
    while(top) {
        unsigned owner=stack[--top]+1;jr_object *o=&r->objects[owner-1];
        unsigned count=o->kind==K_OBJECT_ARRAY?(unsigned)o->length:o->kind==K_EXCEPTION&&o->capacity?1:0;
        for(unsigned j=0;j<count;j++) {
            unsigned id=o->kind==K_OBJECT_ARRAY?((uint32_t*)o->data)[j]:(unsigned)o->capacity;
            if(id&&!marked[id-1]){marked[id-1]=1;stack[top++]=id-1;}
        }
        for(unsigned link=heads[owner-1];link;link=next[link-1]) {
            unsigned id=(unsigned)(uintptr_t)r->cells[link-1].value.l;
            if(id&&!marked[id-1]){marked[id-1]=1;stack[top++]=id-1;}
        }
    }
    /* Remove edges as a batch before freeing cyclic nodes; recursive refcount
     * deletion would otherwise observe partially cleared cycles. */
    for(unsigned i=0;i<JR_OBJECTS;i++)if(!marked[i]&&r->objects[i].kind) {
        jr_object *o=&r->objects[i];
        if(o->kind==K_OBJECT_ARRAY)for(jsize j=0;j<o->length;j++){unsigned id=((uint32_t*)o->data)[j];if(id)r->objects[id-1].refs--;}
        else if(o->kind==K_EXCEPTION&&o->capacity)r->objects[(unsigned)o->capacity-1].refs--;
    }
    for(unsigned i=0;i<JR_FIELD_CELLS;i++) {
        jr_field_cell *c=&r->cells[i];
        if(c->object&&!marked[c->object-1]) {
            if(field_is_reference(&r->field[c->field])&&c->value.l)r->objects[(unsigned)(uintptr_t)c->value.l-1].refs--;
            memset(c,0,sizeof(*c));r->field_cells--;
        }
    }
    for(unsigned i=0;i<JR_OBJECTS;i++)if(!marked[i]&&r->objects[i].kind) {
        jr_object *o=&r->objects[i];if(o->kind==K_OBJECT_ARRAY)r->object_arrays--;
        if(o->kind)weak_clear(r,i+1);
        if(o->blocks){memset(r->blocks+o->block,0,o->blocks);r->payload_used-=o->blocks*JR_BLOCK;}
        memset(o,0,sizeof(*o));
    }
}
static void exception(jr_thread *t, unsigned cls) {
    if (t->pending) return;
    t->exception_context[0]=0;
    t->exception_reported=0;
    unsigned id=object_new(t->r,K_EXCEPTION,cls);
    if (!id) id=t->r->oom_object;
    t->r->objects[id-1].refs++;
    t->pending=id;
}
static void named_exception(jr_thread *t,unsigned cls,const char *type,
                            const char *name,const char *signature) {
    if(t->pending)return;
    exception(t,cls);
    snprintf(t->exception_context,sizeof(t->exception_context),"%s%s%s%s%s",
             type?type:"",name?".":"",name?name:"",signature?" ":"",signature?signature:"");
}
static jr_ref *ref_valid(jr_thread *t, jobject handle) {
    uintptr_t bits=(uintptr_t)handle;
    if (!bits) return 0;
    unsigned index=(unsigned)(bits & JR_INDEX_MASK);
    if (bits>UINT32_MAX || !index || index>JR_REFS) return 0;
    jr_ref *ref=&t->r->refs[index-1];
    if (!ref->kind || ref->generation != (bits >> JR_INDEX_BITS)) return NULL;
    if (ref->owner && ref->owner!=t->index+1) return 0;
    return ref;
}
static unsigned ref_object(jr_thread *t,jobject handle){jr_ref *ref=ref_valid(t,handle);return ref?ref->object:0;}
static int ref_is_null(jr_thread *t,jobject handle){jr_ref *ref=ref_valid(t,handle);return !handle||(ref&&ref->kind==JNIWeakGlobalRefType&&!ref->object);}
static jr_object *object_get(jr_thread *t, jobject handle, unsigned kind) {
    unsigned id=ref_object(t,handle);
    if (!id) { exception(t,ref_is_null(t,handle) ? C_NULL : C_ARGUMENT); return NULL; }
    jr_object *o=&t->r->objects[id-1];
    if (kind && o->kind!=kind) { exception(t,C_ARGUMENT); return NULL; }
    return o;
}
static jobject ref_new(jr_thread *t, unsigned id, unsigned kind) {
    if (!id) return NULL;
    acp_jni_runtime *r=t->r;
    jr_frame *f=&t->frames[t->depth];
    int reserved=kind==JNILocalRefType && f->used<f->quota;
    if (!r->free_refs || (!reserved && r->free_refs<=r->reservations)) {
        exception(t,C_OOM); return NULL;
    }
    for (unsigned i=0; i<JR_REFS; i++) {
        jr_ref *ref=&r->refs[i];
        if (ref->kind || ref->generation==JR_GENERATION_MAX) continue;
        ref->generation++;
        ref->object=id; ref->kind=kind;
        ref->owner=kind==JNILocalRefType ? t->index+1 : 0;
        ref->frame=t->depth;
        r->free_refs--;
        if (kind==JNILocalRefType) {
            if (reserved) r->reservations--;
            else f->quota++;
            f->used++;
        }
        if(kind!=JNIWeakGlobalRefType)r->objects[id-1].refs++;
        return (jobject)(uintptr_t)((ref->generation << JR_INDEX_BITS) | (i+1));
    }
    exception(t,C_OOM);
    return NULL;
}
static void ref_drop(acp_jni_runtime *r, unsigned index) {
    jr_ref *ref=&r->refs[index];
    /* Releasing a leaf (normally a manifest String) cannot make a reference
     * cycle unreachable. Inspect before object_drop recursively clears it. */
    int cycles=ref->kind!=JNIWeakGlobalRefType&&object_has_links(r,ref->object);
    if (ref->owner) {
        jr_frame *f=&r->threads[ref->owner-1].frames[ref->frame];
        f->used--;
        if (ref->generation<JR_GENERATION_MAX) r->reservations++;
        else f->quota--; /* retire a handle slot permanently before wraparound */
    }
    if(ref->kind!=JNIWeakGlobalRefType)object_drop(r,ref->object);
    ref->object=ref->kind=0;
    if (ref->generation<JR_GENERATION_MAX) r->free_refs++;
    if(cycles)collect_array_cycles(r);
}
static int capacity(jr_thread *t, jint count) {
    if (count<0) { exception(t,C_ARGUMENT); return JNI_ERR; }
    jr_frame *f=&t->frames[t->depth];
    unsigned have=f->quota-f->used;
    unsigned add=(unsigned)count>have ? (unsigned)count-have : 0;
    if (add>t->r->free_refs-t->r->reservations) { exception(t,C_OOM); return JNI_ERR; }
    f->quota+=add; t->r->reservations+=add;
    return JNI_OK;
}
static jint j_GetVersion(JNIEnv *env) { jr_thread *t=enter(env); leave(t); return JNI_VERSION_1_6; }
static jclass j_FindClass(JNIEnv *env, const char *name) {
    jr_thread *t=enter(env);
    int c=resolve_class(t->r,name);
    jclass result=NULL;
    if (c<0) named_exception(t,C_NOCLASS,name,NULL,NULL);
    else result=ref_new(t,t->r->types[c].object,JNILocalRefType);
    leave(t); return result;
}
static jobject j_NewLocalRef(JNIEnv *env, jobject obj) {
    jr_thread *t=enter(env); unsigned id=ref_object(t,obj);
    if (obj && !ref_valid(t,obj)) exception(t,C_ARGUMENT);
    jobject result=ref_new(t,id,JNILocalRefType); leave(t); return result;
}
static jobject j_NewGlobalRef(JNIEnv *env, jobject obj) {
    jr_thread *t=enter(env); unsigned id=ref_object(t,obj);
    if (obj && !ref_valid(t,obj)) exception(t,C_ARGUMENT);
    jobject result=ref_new(t,id,JNIGlobalRefType); leave(t); return result;
}
static void delete_ref(JNIEnv *env, jobject obj, unsigned kind) {
    jr_thread *t=enter(env);
    if (obj) {
        jr_ref *ref=ref_valid(t,obj);unsigned index=((uintptr_t)obj & JR_INDEX_MASK)-1;
        if (!ref || ref->kind!=kind) exception(t,C_ARGUMENT);
        else ref_drop(t->r,index);
    }
    leave(t);
}
static void j_DeleteLocalRef(JNIEnv *env, jobject obj) { delete_ref(env,obj,JNILocalRefType); }
static void j_DeleteGlobalRef(JNIEnv *env, jobject obj) { delete_ref(env,obj,JNIGlobalRefType); }
static jweak j_NewWeakGlobalRef(JNIEnv *env,jobject obj){jr_thread *t=enter(env);jr_ref *ref=ref_valid(t,obj);if(obj&&!ref)exception(t,C_ARGUMENT);jweak out=ref_new(t,ref?ref->object:0,JNIWeakGlobalRefType);leave(t);return out;}
static void j_DeleteWeakGlobalRef(JNIEnv *env,jweak obj){delete_ref(env,obj,JNIWeakGlobalRefType);}
static jobjectRefType j_GetObjectRefType(JNIEnv *env, jobject obj) {
    jr_thread *t=enter(env); jr_ref *ref=ref_valid(t,obj);
    jobjectRefType type=ref ? ref->kind : JNIInvalidRefType;
    leave(t); return type;
}
static jboolean j_IsSameObject(JNIEnv *env, jobject a, jobject b) {
    jr_thread *t=enter(env); unsigned x=ref_object(t,a), y=ref_object(t,b);
    int valid=(!a || ref_valid(t,a)) && (!b || ref_valid(t,b));
    if (!valid) exception(t,C_ARGUMENT);
    leave(t); return valid && x==y;
}
static jint j_EnsureLocalCapacity(JNIEnv *env, jint n) {
    jr_thread *t=enter(env); int result=capacity(t,n); leave(t); return result;
}
static jint j_PushLocalFrame(JNIEnv *env, jint n) {
    jr_thread *t=enter(env); int result=JNI_ERR;
    if (n<0) exception(t,C_ARGUMENT);
    else if (t->depth+1==JR_FRAMES) exception(t,C_OOM);
    else {
        t->frames[++t->depth]=(jr_frame){0};
        result=capacity(t,n);
        if (result!=JNI_OK) t->depth--;
    }
    leave(t); return result;
}
static void clear_frame(jr_thread *t) {
    for (unsigned i=0; i<JR_REFS; i++) {
        jr_ref *ref=&t->r->refs[i];
        if (ref->object && ref->owner==t->index+1 && ref->frame==t->depth) ref_drop(t->r,i);
    }
    t->r->reservations-=t->frames[t->depth].quota;
    t->frames[t->depth]=(jr_frame){0};
}
static jobject j_PopLocalFrame(JNIEnv *env, jobject result) {
    jr_thread *t=enter(env); jobject out=NULL;
    unsigned id=ref_object(t,result);
    if (t->calls && t->depth==t->call_frames[t->calls-1]) exception(t,C_STATE);
    else if (!t->depth || (result && !ref_valid(t,result))) exception(t,C_ARGUMENT);
    else {
        if (id) t->r->objects[id-1].refs++; /* keep result alive while releasing frame */
        clear_frame(t); t->depth--;
        out=ref_new(t,id,JNILocalRefType);
        object_drop(t->r,id);
    }
    leave(t); return out;
}
static jclass j_GetObjectClass(JNIEnv *env, jobject obj) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,obj,0);
    jclass result=o ? ref_new(t,t->r->types[o->cls].object,JNILocalRefType) : NULL;
    leave(t); return result;
}
static int class_from_object(jr_thread *t, jclass cls) {
    jr_object *o=object_get(t,cls,K_CLASS);
    return o ? (int)o->length : -1;
}
static jclass j_GetSuperclass(JNIEnv *env, jclass cls) {
    jr_thread *t=enter(env); int c=class_from_object(t,cls);
    int parent=c<0 ? -1 : t->r->types[c].parent;
    jclass result=parent<0 ? NULL : ref_new(t,t->r->types[parent].object,JNILocalRefType);
    leave(t); return result;
}
static jboolean j_IsAssignableFrom(JNIEnv *env, jclass sub, jclass sup) {
    jr_thread *t=enter(env); int a=class_from_object(t,sub), b=class_from_object(t,sup);
    int result=a>=0 && b>=0 && assignable(t->r,a,b);
    leave(t); return result;
}
static jboolean j_IsInstanceOf(JNIEnv *env, jobject obj, jclass cls) {
    jr_thread *t=enter(env); int c=class_from_object(t,cls);
    int is_null=ref_is_null(t,obj);jr_object *o=is_null ? NULL : object_get(t,obj,0);
    int result=c>=0 && (is_null || (o && assignable(t->r,o->cls,c)));
    leave(t); return result;
}
static jint j_GetJavaVM(JNIEnv *env, JavaVM **vm) {
    jr_thread *t=enter(env);
    if (vm) *vm=&t->r->vm;
    leave(t); return vm ? JNI_OK : JNI_ERR;
}
static jint j_RegisterNatives(JNIEnv *env, jclass cls, const JNINativeMethod *methods, jint n) {
    jr_thread *t=enter(env); int c=class_from_object(t,cls), result=JNI_ERR;
    if (c>=0) {
        int code=acp_jni_register(t->r->registry,t->r->types[c].name,methods,n,
                                t->r->ops.code_check,t->r->ops.code_ctx);
        if (code==ACP_JNI_OK) result=JNI_OK;
        else exception(t,code==ACP_JNI_UNKNOWN ? C_NOMETHOD : C_ARGUMENT);
    }
    leave(t); return result;
}
static jint j_UnregisterNatives(JNIEnv *env, jclass cls) {
    jr_thread *t=enter(env); int c=class_from_object(t,cls), result=JNI_ERR;
    if (c>=0) result=acp_jni_unregister(t->r->registry,t->r->types[c].name)==ACP_JNI_OK ? JNI_OK : JNI_ERR;
    leave(t); return result;
}
static jboolean j_ExceptionCheck(JNIEnv *env) {
    jr_thread *t=enter(env); int result=t->pending!=0; leave(t); return result;
}
static jthrowable j_ExceptionOccurred(JNIEnv *env) {
    jr_thread *t=enter(env); jobject result=ref_new(t,t->pending,JNILocalRefType);
    leave(t); return result;
}
static void j_ExceptionClear(JNIEnv *env) {
    jr_thread *t=enter(env); object_drop(t->r,t->pending); t->pending=0;
    t->exception_context[0]=0;leave(t);
}
static void j_ExceptionDescribe(JNIEnv *env) {
    jr_thread *t=enter(env);acp_jni_runtime *r=t->r;
    if(!t->pending){leave(t);return;}
    jr_object *error=&r->objects[t->pending-1];
    const char *message=t->exception_context;
    if(error->kind==K_EXCEPTION&&error->capacity) {
        jr_object *text=&r->objects[(unsigned)error->capacity-1];
        if(text->kind==K_STRING)message=(char*)text->data+(size_t)text->length*sizeof(jchar);
    }
    char output[1024];
    snprintf(output,sizeof(output),"%s%s%s",r->types[error->cls].name,
             *message?": ":"",message);
    /* Clear the pending root only. ExceptionOccurred/global references still
     * retain their Throwable and its message after this call. */
    object_drop(r,t->pending);t->pending=0;t->exception_context[0]=0;
    void (*report)(void *,const char *)=r->exception_report;
    void *ctx=r->exception_report_ctx;
    leave(t);
    if(report)report(ctx,output);else fprintf(stderr,"%s\n",output);
}
int acp_jni_runtime_exception_reporter(acp_jni_runtime *r,
                                     void (*report)(void *,const char *),void *ctx) {
    if(!r)return JNI_EINVAL;
    r->ops.lock(r->ops.ctx);
    int result=JNI_ERR;
    if(!r->sealed){r->exception_report=report;r->exception_report_ctx=ctx;result=JNI_OK;}
    r->ops.unlock(r->ops.ctx);
    return result;
}
int acp_jni_runtime_trace_exceptions(acp_jni_runtime *r,int enabled){
    if(!r)return JNI_EINVAL;
    r->ops.lock(r->ops.ctx);r->trace_exceptions=!!enabled;r->ops.unlock(r->ops.ctx);return JNI_OK;
}
static jint j_Throw(JNIEnv *env, jthrowable throwable) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,throwable,0); int result=JNI_ERR;
    if (o && assignable(t->r,o->cls,C_THROWABLE)) {
        unsigned id=ref_object(t,throwable);
        o->refs++; object_drop(t->r,t->pending); t->pending=id;
        t->exception_context[0]=0;t->exception_reported=0;result=JNI_OK;
    } else if (o) exception(t,C_ARGUMENT);
    leave(t); return result;
}
static jint j_ThrowNew(JNIEnv *env,jclass cls,const char *message) {
    jr_thread *t=enter(env);int c=class_from_object(t,cls);
    int valid=c>=0&&assignable(t->r,(unsigned)c,C_THROWABLE)&&!t->pending;
    if(c>=0&&!assignable(t->r,(unsigned)c,C_THROWABLE))exception(t,C_ARGUMENT);
    leave(t);if(!valid)return JNI_ERR;
    if((*env)->PushLocalFrame(env,4)!=JNI_OK)return JNI_ERR;
    jmethodID ctor=(*env)->GetMethodID(env,cls,"<init>","(Ljava/lang/String;)V");
    jstring text=NULL; jthrowable obj=NULL; int result=JNI_ERR;
    if(ctor&&message)text=(*env)->NewStringUTF(env,message);
    if(ctor&&!(*env)->ExceptionCheck(env)) {
        jvalue arg={.l=text};obj=(*env)->NewObjectA(env,cls,ctor,&arg);
        if(obj&&!(*env)->ExceptionCheck(env))result=(*env)->Throw(env,obj);
    }
    (void)(*env)->PopLocalFrame(env,NULL);return result;
}
static void j_FatalError(JNIEnv *env, const char *message) { unsupported(env,message ? message : "JNI FatalError"); }

static unsigned utf_bytes(jchar ch) { return ch && ch<0x80 ? 1 : ch<0x800 ? 2 : 3; }
static char *utf_put(char *dst, jchar ch) {
    if (ch && ch<0x80) *dst++=(char)ch;
    else if (ch<0x800) { *dst++=(char)(0xc0 | (ch>>6)); *dst++=(char)(0x80 | (ch&63)); }
    else { *dst++=(char)(0xe0 | (ch>>12)); *dst++=(char)(0x80 | ((ch>>6)&63)); *dst++=(char)(0x80 | (ch&63)); }
    return dst;
}
/* Modified UTF-8 encodes NUL as C0 80 and UTF-16 surrogate code units separately.
 * Four-byte standard UTF-8 and other overlong encodings are rejected. */
static int utf_next(const unsigned char **cursor, jchar *out) {
    const unsigned char *p=*cursor; unsigned a=*p++, v;
    if (a<0x80) v=a;
    else if (a>=0xc0 && a<0xe0) {
        unsigned b=*p; if ((b&0xc0)!=0x80) return 0; p++;
        v=((a&31)<<6) | (b&63);
        if (v<0x80 && v!=0) return 0;
    } else if (a>=0xe0 && a<0xf0) {
        unsigned b=*p; if ((b&0xc0)!=0x80) return 0; p++;
        unsigned c=*p; if ((c&0xc0)!=0x80) return 0; p++;
        v=((a&15)<<12) | ((b&63)<<6) | (c&63);
        if (v<0x800) return 0;
    } else return 0;
    *out=(jchar)v; *cursor=p; return 1;
}
static void *payload_new(jr_thread *t, unsigned id, size_t bytes) {
    acp_jni_runtime *r=t->r;
    if (bytes>sizeof(r->payload)) { exception(t,C_OOM); return NULL; }
    unsigned need=(unsigned)((bytes+JR_BLOCK-1)/JR_BLOCK), run=0;
    for (unsigned i=0; i<JR_BLOCKS; i++) {
        run=r->blocks[i] ? 0 : run+1;
        if (run==need) {
            unsigned start=i+1-need;
            memset(r->blocks+start,1,need);
            r->objects[id-1].block=start; r->objects[id-1].blocks=need;
            r->payload_used+=need*JR_BLOCK;
            if (r->payload_used>r->payload_peak) r->payload_peak=r->payload_used;
            return r->payload+start*JR_BLOCK;
        }
    }
    exception(t,C_OOM); return NULL;
}
static unsigned string_object(jr_thread *t, size_t chars, size_t bytes) {
    if (chars>INT_MAX || bytes>INT_MAX || chars>(SIZE_MAX-bytes-1)/sizeof(jchar)) {
        exception(t,C_OOM); return 0;
    }
    unsigned id=object_new(t->r,K_STRING,C_STRING);
    if (!id) { exception(t,C_OOM); return 0; }
    jr_object *o=&t->r->objects[id-1];
    o->data=payload_new(t,id,chars*sizeof(jchar)+bytes+1);
    if (!o->data) { object_collect(t->r,id); return 0; }
    o->length=(jsize)chars; o->utf_length=(jsize)bytes;
    return id;
}
static jstring string_ref(jr_thread *t, unsigned id) {
    jstring result=ref_new(t,id,JNILocalRefType);
    if (id && !result) object_collect(t->r,id);
    return result;
}
static jstring j_NewString(JNIEnv *env, const jchar *chars, jsize len) {
    jr_thread *t=enter(env); jstring result=NULL;
    if (len<0 || (!chars && len)) exception(t,C_ARGUMENT);
    else if ((unsigned)len>sizeof(t->r->payload)/sizeof(jchar)) exception(t,C_OOM);
    else {
        size_t bytes=0;
        for (jsize i=0; i<len; i++) bytes+=utf_bytes(chars[i]);
        unsigned id=string_object(t,(size_t)len,bytes);
        if (id) {
            jr_object *o=&t->r->objects[id-1];
            if (len) memcpy(o->data,chars,(size_t)len*sizeof(jchar));
            char *out=(char *)o->data+(size_t)len*sizeof(jchar);
            for (jsize i=0; i<len; i++) out=utf_put(out,chars[i]);
            *out=0; result=string_ref(t,id);
        }
    }
    leave(t); return result;
}
static jstring j_NewStringUTF(JNIEnv *env, const char *utf) {
    jr_thread *t=enter(env); jstring result=NULL;
    /* Android accepts NULL as a null jstring. The original ShowPopup passes
     * absent optional button labels through NewStringUTF unconditionally. */
    if (utf) {
        const unsigned char *start=(const unsigned char *)utf, *p=start;
        size_t chars=0; jchar ch; int ok=1;
        while (*p) {
            if ((size_t)(p-start)>=sizeof(t->r->payload)) { exception(t,C_OOM); ok=0; break; }
            if (!utf_next(&p,&ch)) { exception(t,C_ARGUMENT); ok=0; break; }
            chars++;
        }
        if (ok) {
            size_t bytes=(size_t)(p-start);
            unsigned id=string_object(t,chars,bytes);
            if (id) {
                jr_object *o=&t->r->objects[id-1]; jchar *out=o->data; p=start;
                while (*p) { (void)utf_next(&p,&ch); *out++=ch; }
                memcpy(out,utf,bytes+1); result=string_ref(t,id);
            }
        }
    }
    leave(t); return result;
}
static jsize string_length(JNIEnv *env, jstring str, int utf) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,str,K_STRING);
    jsize result=o ? (utf ? o->utf_length : o->length) : 0;
    leave(t); return result;
}
static jsize j_GetStringLength(JNIEnv *e, jstring s) { return string_length(e,s,0); }
static jsize j_GetStringUTFLength(JNIEnv *e, jstring s) { return string_length(e,s,1); }
static const void *string_pin(JNIEnv *env, jstring str, jboolean *copy, int utf) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,str,K_STRING); const void *result=NULL;
    if (o) {
        unsigned id=ref_object(t,str); jr_pin *pin=NULL, *empty=NULL;
        for (unsigned i=0; i<JR_PINS; i++) {
            jr_pin *p=&t->r->pins[i];
            if (!p->count && !empty) empty=p;
            if (p->count && p->object==id && p->owner==t->index && p->utf==(unsigned)utf) pin=p;
        }
        if (!pin) pin=empty;
        if (!pin || o->pins==UINT_MAX || t->pins==UINT_MAX) exception(t,C_OOM);
        else {
            if (!pin->count) *pin=(jr_pin){.object=id,.owner=t->index,.utf=(unsigned)utf};
            pin->count++;
            o->pins++; t->pins++;
            result=(unsigned char *)o->data+(utf ? (size_t)o->length*sizeof(jchar) : 0);
            if (copy) *copy=JNI_FALSE;
        }
    }
    leave(t); return result;
}
static const jchar *j_GetStringChars(JNIEnv *e, jstring s, jboolean *c) { return string_pin(e,s,c,0); }
static const char *j_GetStringUTFChars(JNIEnv *e, jstring s, jboolean *c) { return string_pin(e,s,c,1); }
static const jchar *j_GetStringCritical(JNIEnv *e, jstring s, jboolean *c) { return string_pin(e,s,c,0); }
static void string_release(JNIEnv *env, jstring str, const void *ptr, int utf) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,str,K_STRING);
    if (o) {
        unsigned id=ref_object(t,str); jr_pin *pin=NULL;
        for (unsigned i=0; i<JR_PINS; i++) {
            jr_pin *p=&t->r->pins[i];
            if (p->count && p->object==id && p->owner==t->index && p->utf==(unsigned)utf) pin=p;
        }
        const void *expected=(unsigned char *)o->data+(utf ? (size_t)o->length*sizeof(jchar) : 0);
        if (ptr!=expected || !pin) exception(t,C_ARGUMENT);
        else { pin->count--; o->pins--; t->pins--; object_collect(t->r,id); }
    }
    leave(t);
}
static void j_ReleaseStringChars(JNIEnv *e, jstring s, const jchar *c) { string_release(e,s,c,0); }
/* The bundled Android JNI header uses char*, unlike newer const-correct headers. */
static void j_ReleaseStringUTFChars(JNIEnv *e, jstring s, char *c) { string_release(e,s,c,1); }
static void j_ReleaseStringCritical(JNIEnv *e, jstring s, const jchar *c) { string_release(e,s,c,0); }
static void string_region(JNIEnv *env, jstring str, jsize start, jsize len, void *out, int utf) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,str,K_STRING);
    if (o) {
        if (start<0 || len<0 || start>o->length || len>o->length-start) exception(t,C_BOUNDS);
        else if (len && !out) exception(t,C_NULL);
        else if (len) {
            const jchar *chars=(const jchar *)o->data+start;
            if (!utf) memcpy(out,chars,(size_t)len*sizeof(jchar));
            else { char *p=out; for (jsize i=0; i<len; i++) p=utf_put(p,chars[i]); }
        }
    }
    leave(t);
}
static void j_GetStringRegion(JNIEnv *e,jstring s,jsize a,jsize n,jchar *b) { string_region(e,s,a,n,b,0); }
static void j_GetStringUTFRegion(JNIEnv *e,jstring s,jsize a,jsize n,char *b) { string_region(e,s,a,n,b,1); }
#include "acp_jni_arrays.inc"
static jobject j_NewDirectByteBuffer(JNIEnv *env, void *address, jlong size) {
    jr_thread *t=enter(env); jobject result=NULL;
    if (size<0 || size>INT_MAX || (!address && size)) exception(t,C_ARGUMENT);
    else {
        unsigned id=object_new(t->r,K_BUFFER,C_BYTEBUFFER);
        if (!id) exception(t,C_OOM);
        else {
            t->r->objects[id-1].data=address; t->r->objects[id-1].capacity=size;
            result=ref_new(t,id,JNILocalRefType); if (!result) object_collect(t->r,id);
        }
    }
    leave(t); return result;
}
static void *j_GetDirectBufferAddress(JNIEnv *env, jobject buf) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,buf,0);
    void *result=o && o->kind==K_BUFFER ? o->data : NULL;
    leave(t); return result;
}
static jlong j_GetDirectBufferCapacity(JNIEnv *env, jobject buf) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,buf,0);
    jlong result=o && o->kind==K_BUFFER ? o->capacity : -1;
    leave(t); return result;
}
int acp_jni_runtime_monitors(acp_jni_runtime *r,const acp_jni_monitor_ops *ops) {
    if(!r||!ops||!ops->wait||!ops->wake_all)return JNI_EINVAL;
    r->ops.lock(r->ops.ctx);int result=JNI_ERR;
    if(!r->sealed&&!r->monitors.wait){r->monitors=*ops;result=JNI_OK;}
    r->ops.unlock(r->ops.ctx);return result;
}
static jint j_MonitorEnter(JNIEnv *env,jobject obj) {
    jr_thread *t=enter(env);acp_jni_runtime *r=t->r;jr_object *o=object_get(t,obj,0);int result=JNI_ERR;
    if(o) {
        if(!r->monitors.wait){leave(t);fail(r,"JNI monitor backend has not been installed");}
        unsigned id=ref_object(t,obj);o->refs++; /* Root throughout the wait. */
        while(o->monitor_owner&&o->monitor_owner!=t->index+1) {
            if(r->monitors.wait(r->monitors.ctx)){exception(t,C_STATE);object_drop(r,id);goto done;}
        }
        if(o->monitor_depth==UINT_MAX){exception(t,C_OOM);object_drop(r,id);goto done;}
        if(o->monitor_owner)object_drop(r,id); /* Existing root covers recursion. */
        o->monitor_owner=t->index+1;o->monitor_depth++;result=JNI_OK;
    }
done:
    leave(t);return result;
}
static jint j_MonitorExit(JNIEnv *env,jobject obj) {
    jr_thread *t=enter(env);jr_object *o=object_get(t,obj,0);int result=JNI_ERR;
    if(o) {
        if(o->monitor_owner!=t->index+1||!o->monitor_depth)exception(t,C_MONITOR);
        else {
            result=JNI_OK;
            if(!--o->monitor_depth) {
                o->monitor_owner=0;object_drop(t->r,ref_object(t,obj));collect_array_cycles(t->r);
                t->r->monitors.wake_all(t->r->monitors.ctx);
            }
        }
    }
    leave(t);return result;
}

static int version_supported(jint version) {
    return version==JNI_VERSION_1_1 || version==JNI_VERSION_1_2 ||
           version==JNI_VERSION_1_4 || version==JNI_VERSION_1_6;
}
static jr_thread *current(acp_jni_runtime *r) {
    uintptr_t id=r->ops.thread_id(r->ops.ctx);
    if (!id) return NULL;
    for (unsigned i=0; i<JR_THREADS; i++) if (r->threads[i].id==id) return &r->threads[i];
    return NULL;
}
static jint vm_attach(JavaVM *vm, JNIEnv **out, void *args) {
    acp_jni_runtime *r=(acp_jni_runtime *)vm;
    if (!out) return JNI_EINVAL;
    *out=NULL;
    JavaVMAttachArgs *a=args;
    if (a && (!version_supported(a->version) || a->version==JNI_VERSION_1_1)) return JNI_EVERSION;
    if (a && a->group) return JNI_EINVAL; /* ThreadGroup not implemented */
    r->ops.lock(r->ops.ctx);
    jr_thread *t=current(r);
    if (t) { *out=&t->env; r->ops.unlock(r->ops.ctx); return JNI_OK; }
    uintptr_t id=r->ops.thread_id(r->ops.ctx);
    if (!id) { r->ops.unlock(r->ops.ctx); return JNI_ERR; }
    if (r->free_refs-r->reservations<16) { r->ops.unlock(r->ops.ctx); return JNI_ENOMEM; }
    for (unsigned i=0; i<JR_THREADS; i++) if (!r->threads[i].id) {
        t=&r->threads[i];
        *t=(jr_thread){.env=&jr_table,.r=r,.id=id,.index=i};
        t->frames[0].quota=16; r->reservations+=16; r->live_threads++;
        r->sealed=1; *out=&t->env; r->ops.unlock(r->ops.ctx); return JNI_OK;
    }
    r->ops.unlock(r->ops.ctx); return JNI_ENOMEM;
}
static jint vm_getenv(JavaVM *vm, void **out, jint version) {
    acp_jni_runtime *r=(acp_jni_runtime *)vm;
    if (!out) return JNI_EINVAL;
    *out=NULL;
    if (!version_supported(version)) return JNI_EVERSION;
    r->ops.lock(r->ops.ctx); jr_thread *t=current(r);
    if (t) *out=&t->env;
    r->ops.unlock(r->ops.ctx); return t ? JNI_OK : JNI_EDETACHED;
}
static jint vm_detach(JavaVM *vm) {
    acp_jni_runtime *r=(acp_jni_runtime *)vm;
    r->ops.lock(r->ops.ctx); jr_thread *t=current(r);
    if (!t) { r->ops.unlock(r->ops.ctx); return JNI_EDETACHED; }
    if (t->pins || t->calls) { r->ops.unlock(r->ops.ctx); return JNI_ERR; }
    for (;;) { clear_frame(t); if (!t->depth) break; t->depth--; }
    int released_monitors=0;
    for(unsigned i=0;i<JR_OBJECTS;i++)if(r->objects[i].monitor_owner==t->index+1) {
        r->objects[i].monitor_owner=r->objects[i].monitor_depth=0;object_drop(r,i+1);released_monitors=1;
    }
    if(released_monitors){collect_array_cycles(r);r->monitors.wake_all(r->monitors.ctx);}
    object_drop(r,t->pending); t->pending=0; t->id=0; r->live_threads--;
    r->ops.unlock(r->ops.ctx); return JNI_OK;
}
static jint vm_destroy(JavaVM *vm) { (void)vm; return JNI_ERR; /* host controls teardown */ }
static const struct JNIInvokeInterface jr_vm_table={
    .DestroyJavaVM=vm_destroy,.AttachCurrentThread=vm_attach,.DetachCurrentThread=vm_detach,
    .GetEnv=vm_getenv,.AttachCurrentThreadAsDaemon=vm_attach
};
static int add_class(acp_jni_runtime *r, const char *name, int parent) {
    size_t n=name ? strlen(name) : 0;
    if (!n || n>=ACP_JNI_CLASS_NAME || r->classes==JR_CLASSES || class_index(r,name)>=0) return JNI_ERR;
    unsigned id=object_new(r,K_CLASS,C_CLASS);
    if (!id) return JNI_ENOMEM;
    unsigned c=r->classes++;
    strcpy(r->types[c].name,name); r->types[c].parent=parent; r->types[c].object=id;
    r->objects[id-1].length=(jsize)c; r->objects[id-1].refs=1; /* class loader root */
    return JNI_OK;
}
static jvalue throwable_construct(JNIEnv *env,jobject object,const jvalue *args,unsigned count,void *ctx) {
    (void)ctx;jvalue result={0};jr_thread *t=enter(env);jr_object *o=object_get(t,object,0);
    unsigned message=count?ref_object(t,args[0].l):0;
    if(o&&assignable(t->r,o->cls,C_THROWABLE)) {
        if(message)t->r->objects[message-1].refs++;
        if(o->kind==K_EXCEPTION)object_drop(t->r,(unsigned)o->capacity);
        o->kind=K_EXCEPTION;o->capacity=message;
    } else if(o)exception(t,C_ARGUMENT);
    leave(t);return result;
}
static jvalue throwable_message(JNIEnv *env,jobject object,const jvalue *args,unsigned count,void *ctx) {
    (void)args;(void)count;(void)ctx;jvalue result={0};jr_thread *t=enter(env);
    jr_object *o=object_get(t,object,0);
    if(o&&o->kind==K_EXCEPTION)result.l=ref_new(t,(unsigned)o->capacity,JNILocalRefType);
    leave(t);return result;
}
size_t acp_jni_runtime_size(void) { return sizeof(acp_jni_runtime); }
acp_jni_runtime *acp_jni_runtime_create(void *storage, size_t bytes,
                                     acp_jni_registry *registry, const acp_jni_ops *ops) {
    if (!storage || bytes<sizeof(acp_jni_runtime) || (uintptr_t)storage % _Alignof(acp_jni_runtime) ||
        !registry || !ops || !ops->thread_id || !ops->lock || !ops->unlock ||
        !ops->fatal || !ops->code_check) return NULL;
    acp_jni_runtime *r=storage; memset(r,0,sizeof(*r));
    r->ops=*ops; r->registry=registry;
    r->vm=&jr_vm_table; r->free_refs=JR_REFS;
    static const struct { const char *name; int parent; } builtins[]={
        {"java/lang/Object",-1},{"java/lang/Class",C_OBJECT},{"java/lang/String",C_OBJECT},
        {"java/lang/Throwable",C_OBJECT},{"java/lang/Error",C_THROWABLE},
        {"java/lang/Exception",C_THROWABLE},{"java/lang/RuntimeException",C_EXCEPTION},
        {"java/lang/OutOfMemoryError",C_ERROR},{"java/lang/NoClassDefFoundError",C_LINKAGE},
        {"java/lang/NoSuchMethodError",C_INCOMPATIBLE},{"java/lang/IllegalArgumentException",C_RUNTIME},
        {"java/lang/StringIndexOutOfBoundsException",C_INDEX},{"java/lang/IllegalStateException",C_RUNTIME},
        {"java/lang/NullPointerException",C_RUNTIME},{"java/nio/Buffer",C_OBJECT},{"java/nio/ByteBuffer",C_BUFFER},
        {"java/lang/LinkageError",C_ERROR},{"java/lang/IncompatibleClassChangeError",C_LINKAGE},
        {"java/lang/IndexOutOfBoundsException",C_RUNTIME},
        {"java/lang/NegativeArraySizeException",C_RUNTIME},{"java/lang/ArrayIndexOutOfBoundsException",C_INDEX},
        {"java/lang/ArrayStoreException",C_RUNTIME},
        {"java/lang/NoSuchFieldError",C_INCOMPATIBLE},
        {"java/lang/IllegalMonitorStateException",C_RUNTIME},
        {"[Z",C_OBJECT},{"[B",C_OBJECT},{"[C",C_OBJECT},{"[S",C_OBJECT},
        {"[I",C_OBJECT},{"[J",C_OBJECT},{"[F",C_OBJECT},{"[D",C_OBJECT},
        {"java/lang/Cloneable",C_OBJECT},{"java/io/Serializable",C_OBJECT},
        {"java/lang/reflect/Method",C_OBJECT},{"java/lang/reflect/Constructor",C_OBJECT},{"java/lang/reflect/Field",C_OBJECT}
    };
    for (unsigned i=0; i<sizeof(builtins)/sizeof(*builtins); i++)
        if (add_class(r,builtins[i].name,builtins[i].parent)!=JNI_OK) abort();
    r->oom_object=object_new(r,K_EXCEPTION,C_OOM); r->objects[r->oom_object-1].refs=1;
    for(unsigned i=0;i<r->classes;i++)if(assignable(r,i,C_THROWABLE)) {
        acp_jni_method_def defs[]={
            {r->types[i].name,"<init>","()V",0,throwable_construct,NULL},
            {r->types[i].name,"<init>","(Ljava/lang/String;)V",0,throwable_construct,NULL}
        };
        if(acp_jni_runtime_methods(r,defs,2)!=JNI_OK)abort();
    }
    acp_jni_method_def message={"java/lang/Throwable","getMessage","()Ljava/lang/String;",0,throwable_message,NULL};
    if(acp_jni_runtime_methods(r,&message,1)!=JNI_OK)abort();
    return r;
}
int acp_jni_runtime_class(acp_jni_runtime *r, const char *name, const char *parent) {
    r->ops.lock(r->ops.ctx); int p=class_index(r,parent), result=JNI_ERR;
    if (!r->sealed && p>=0) result=add_class(r,name,p);
    r->ops.unlock(r->ops.ctx); return result;
}
int acp_jni_runtime_exception_class(acp_jni_runtime *r,const char *name,const char *parent){
    if(!r||!name||!parent)return JNI_EINVAL;
    r->ops.lock(r->ops.ctx);int p=class_index(r,parent);
    int valid=!r->sealed&&p>=0&&assignable(r,(unsigned)p,C_THROWABLE);
    r->ops.unlock(r->ops.ctx);if(!valid)return JNI_ERR;
    if(acp_jni_runtime_class(r,name,parent)!=JNI_OK)return JNI_ERR;
    const acp_jni_method_def methods[]={
        {name,"<init>","()V",0,throwable_construct,NULL},
        {name,"<init>","(Ljava/lang/String;)V",0,throwable_construct,NULL}
    };
    return acp_jni_runtime_methods(r,methods,2);
}
JavaVM *acp_jni_runtime_vm(acp_jni_runtime *r) { return &r->vm; }
jobject acp_jni_runtime_object(JNIEnv *env, const char *name, void *data) {
    jr_thread *t=enter(env); int c=class_index(t->r,name); jobject result=NULL;
    if (c<0) exception(t,C_NOCLASS);
    else if (c==C_CLASS || c==C_STRING || t->r->types[c].name[0]=='[' || assignable(t->r,c,C_BUFFER)) exception(t,C_ARGUMENT);
    else {
        unsigned id=object_new(t->r,K_OBJECT,c);
        if (!id) exception(t,C_OOM);
        else {
            t->r->objects[id-1].data=data; result=ref_new(t,id,JNILocalRefType);
            if (!result) object_collect(t->r,id);
        }
    }
    leave(t); return result;
}
void *acp_jni_runtime_data(JNIEnv *env, jobject obj, const char *expected) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,obj,K_OBJECT);
    int c=class_index(t->r,expected); void *result=NULL;
    if (o && c>=0 && assignable(t->r,o->cls,c)) result=o->data;
    else if (o) exception(t,C_ARGUMENT);
    leave(t); return result;
}
int acp_jni_runtime_set_data(JNIEnv *env, jobject obj, const char *expected, void *data) {
    jr_thread *t=enter(env); jr_object *o=object_get(t,obj,K_OBJECT);
    int c=class_index(t->r,expected), result=JNI_ERR;
    if (o && c>=0 && assignable(t->r,o->cls,c)) { o->data=data; result=JNI_OK; }
    else if (o) exception(t,C_ARGUMENT);
    leave(t); return result;
}
int acp_jni_runtime_destroy(acp_jni_runtime *r) {
    r->ops.lock(r->ops.ctx);
    if (r->live_threads) { r->ops.unlock(r->ops.ctx); return JNI_ERR; }
    for (unsigned i=0; i<JR_REFS; i++) if (r->refs[i].kind) {
        r->ops.unlock(r->ops.ctx); return JNI_ERR;
    }
    for (unsigned i=0; i<JR_OBJECTS; i++) if (r->objects[i].pins) {
        r->ops.unlock(r->ops.ctx); return JNI_ERR;
    }
    r->ops.unlock(r->ops.ctx); return JNI_OK;
}
void acp_jni_runtime_usage(acp_jni_runtime *r, acp_jni_usage *out) {
    r->ops.lock(r->ops.ctx); *out=(acp_jni_usage){.threads=r->live_threads,
        .payload_bytes=r->payload_used,.payload_peak=r->payload_peak};
    for (unsigned i=0; i<JR_REFS; i++) out->references+=r->refs[i].kind!=0;
    for (unsigned i=0; i<JR_OBJECTS; i++) out->objects+=r->objects[i].kind!=0;
    r->ops.unlock(r->ops.ctx);
}

#include "acp_jni_methods.inc"
#include "acp_jni_fields.inc"
#include "acp_jni_reflection.inc"

/* Generated from the same Android JNI ABI header used to compile this source.
 * Each unsupported slot has its exact prototype and a named fatal path. */
#include "acp_jni_table.inc"
