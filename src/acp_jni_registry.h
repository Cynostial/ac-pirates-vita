#ifndef ACP_JNI_REGISTRY_H
#define ACP_JNI_REGISTRY_H
#include <jni.h>
#include <stdatomic.h>
#include <stdint.h>

#define ACP_JNI_METHODS 96
#define ACP_JNI_CLASSES 16
#define ACP_JNI_CLASS_NAME 128
#define ACP_JNI_METHOD_NAME 64
#define ACP_JNI_SIGNATURE 192
typedef struct {
    char name[ACP_JNI_METHOD_NAME], signature[ACP_JNI_SIGNATURE];
    void *function;
    unsigned class_index;
} acp_jni_native;
typedef struct {
    atomic_flag lock;
    unsigned class_count, method_count;
    char classes[ACP_JNI_CLASSES][ACP_JNI_CLASS_NAME];
    acp_jni_native methods[ACP_JNI_METHODS];
} acp_jni_registry;
enum { ACP_JNI_OK, ACP_JNI_INVALID, ACP_JNI_CAPACITY, ACP_JNI_UNKNOWN, ACP_JNI_ADDRESS };
typedef int (*acp_jni_code_check)(void *ctx, const void *function);

void acp_jni_registry_init(acp_jni_registry *);
/* Declare methods from the inspected Java classes before loading libraries.
 * All names/signatures are copied into fixed storage. No fabricated methods. */
int acp_jni_declare(acp_jni_registry *, const char *class_name, const char *name, const char *signature);
/* RegisterNatives core: preflight the whole batch, then commit. The actual JNI
 * object layer must resolve jclass -> class_name and raise NoSuchMethodError on
 * UNKNOWN; it must not treat jclass as a char*. check must validate the ARM/Thumb
 * address against an audited module's executable PT_LOAD range. Retain bit 0.
 * Returns our status enum, not a JNI status. No code is invoked by this API. */
int acp_jni_register(acp_jni_registry *, const char *class_name,
                    const JNINativeMethod *, jint count, acp_jni_code_check, void *ctx);
int acp_jni_unregister(acp_jni_registry *, const char *class_name);
/* The caller must keep the module loaded until all returned callbacks are idle. */
void *acp_jni_resolve(acp_jni_registry *, const char *class_name, const char *name, const char *signature);
/* Atomically snapshot a callback batch; on any missing method, outputs untouched. */
int acp_jni_snapshot(acp_jni_registry *, const char *class_name,
                    const JNINativeMethod *requested, jint count, void **functions);
int acp_jni_valid_signature(const char *);

#endif
