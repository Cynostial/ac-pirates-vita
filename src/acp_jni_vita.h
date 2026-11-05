#ifndef ACP_JNI_VITA_H
#define ACP_JNI_VITA_H
#include "acp_jni_runtime.h"
#include <psp2/kernel/threadmgr.h>
typedef struct {
    SceUID mutex,monitor_changed;
    void (*report)(void *, const char *);
    void *report_ctx;
} acp_jni_vita;
/* Backend storage must outlive runtime. report is optional and must not reenter
 * JNI. It may log the named failure; the backend then exits the application. */
int acp_jni_vita_ops(acp_jni_vita *, acp_jni_ops *, acp_jni_code_check, void *code_ctx,
                    void (*report)(void *, const char *), void *report_ctx);
int acp_jni_vita_install_monitors(acp_jni_vita *,acp_jni_runtime *);
/* Only after successful acp_jni_runtime_destroy and no native callers remain. */
int acp_jni_vita_dispose(acp_jni_vita *);
#endif
