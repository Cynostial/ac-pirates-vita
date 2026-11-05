#ifndef ACP_JNI_RUNTIME_H
#define ACP_JNI_RUNTIME_H
#include "acp_jni_registry.h"
#include <stddef.h>

/* Bounded JNI substrate reused from local port infrastructure. The caller
 * owns and budgets the fixed storage returned by runtime_size(). Unsupported
 * JNI slots fail explicitly. No Java bytecode or Android VM is executed. */
typedef struct acp_jni_runtime acp_jni_runtime;
typedef struct {
    void *ctx;
    uintptr_t (*thread_id)(void *); /* nonzero, unique for each live native thread */
    void (*lock)(void *);          /* sleeping, non-recursive mutex */
    void (*unlock)(void *);
    void (*fatal)(void *, const char *); /* must not return; abort if it does */
    acp_jni_code_check code_check;
    void *code_ctx;
} acp_jni_ops;

size_t acp_jni_runtime_size(void);
acp_jni_runtime *acp_jni_runtime_create(void *storage, size_t bytes,
                                     acp_jni_registry *, const acp_jni_ops *);
/* Configure inspected class identities/inheritance before attaching any thread.
 * Built-ins: Object, Class, String, Throwable, Error, Exception, RuntimeException,
 * the JNI error classes used here, java/nio/Buffer and java/nio/ByteBuffer. */
int acp_jni_runtime_class(acp_jni_runtime *, const char *name, const char *parent);
/* Declare a standard Throwable subclass with its own no-argument and String
 * constructors. Java constructors are not inherited; ThrowNew needs these. */
int acp_jni_runtime_exception_class(acp_jni_runtime *,const char *name,const char *parent);
/* Inspected Java methods implemented by host services. Definitions are copied
 * into fixed storage before the first attachment and remain immutable thereafter.
 * Callback receives local references in its own frame, outside the VM mutex;
 * it may use JNI recursively. Returned object must be a live JNI reference.
 * Service owns ctx/native_data lifetime; no Java bytecode or arbitrary native
 * signatures are executed by this dispatch layer. Maximum 384 methods/32 args. */
typedef jvalue (*acp_jni_method_fn)(JNIEnv *, jobject, const jvalue *, unsigned, void *);
typedef struct {
    const char *class_name, *name, *signature;
    int is_static;
    acp_jni_method_fn call;
    void *ctx;
} acp_jni_method_def;
int acp_jni_runtime_methods(acp_jni_runtime *, const acp_jni_method_def *, unsigned count);
typedef struct {const char *class_name,*name,*signature;int is_static;} acp_jni_field_def;
/* Declare before first attachment. Values start at Java's zero/null default;
 * initialize inspected Android static/instance values through JNI afterwards. */
int acp_jni_runtime_fields(acp_jni_runtime *,const acp_jni_field_def *,unsigned count);
typedef struct {void *ctx;int (*wait)(void *);void (*wake_all)(void *);} acp_jni_monitor_ops;
/* wait must atomically release the VM mutex and reacquire it before returning.
 * Configure before attachment. No per-object kernel allocation is performed. */
int acp_jni_runtime_monitors(acp_jni_runtime *,const acp_jni_monitor_ops *);
/* ExceptionDescribe writes a bounded description and clears the pending
 * exception, as JNI requires. Configure before attaching any thread.
 * The callback runs outside the VM lock; NULL uses stderr. */
int acp_jni_runtime_exception_reporter(acp_jni_runtime *,
                                     void (*report)(void *,const char *),void *);
/* Also report each newly pending exception once, without clearing it. */
int acp_jni_runtime_trace_exceptions(acp_jni_runtime *,int enabled);
JavaVM *acp_jni_runtime_vm(acp_jni_runtime *);
/* Native service objects hold a borrowed pointer: no ownership of external RAM.
 * Their lifecycle and budget must be managed by the service implementation. */
jobject acp_jni_runtime_object(JNIEnv *, const char *class_name, void *native_data);
void *acp_jni_runtime_data(JNIEnv *, jobject, const char *expected_class);
int acp_jni_runtime_set_data(JNIEnv *, jobject, const char *expected_class, void *);
/* Allocate an empty object array with the prototype's component class. */
jobjectArray acp_jni_runtime_array_like(JNIEnv *,jobjectArray,jsize);
/* Only after all threads detach and every module/callback using this VM is idle.
 * Requires global references released. Returns JNI_ERR instead of invalidating
 * attached threads or borrowed string pointers. Does not free caller storage. */
int acp_jni_runtime_destroy(acp_jni_runtime *);
typedef struct { unsigned threads, references, objects, payload_bytes, payload_peak; } acp_jni_usage;
void acp_jni_runtime_usage(acp_jni_runtime *, acp_jni_usage *);
#endif
