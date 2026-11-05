#include "acp_jni_vita.h"
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>

static void fatal(void *ctx, const char *reason) {
    acp_jni_vita *b=ctx;
    if (b->report) b->report(b->report_ctx,reason);
    sceKernelExitProcess(1);
    abort();
}
static uintptr_t thread_id(void *ctx) {
    (void)ctx; SceUID id=sceKernelGetThreadId();
    return id>0 ? (uintptr_t)id : 0;
}
static void lock(void *ctx) {
    acp_jni_vita *b=ctx;
    if (sceKernelLockMutex(b->mutex,1,NULL)<0) fatal(ctx,"JNI mutex lock failed");
}
static void unlock(void *ctx) {
    acp_jni_vita *b=ctx;
    if (sceKernelUnlockMutex(b->mutex,1)<0) fatal(ctx,"JNI mutex unlock failed");
}
int acp_jni_vita_ops(acp_jni_vita *b, acp_jni_ops *out, acp_jni_code_check check, void *code_ctx,
                    void (*report)(void *, const char *), void *report_ctx) {
    if (!b || !out || !check) return JNI_ERR;
    SceUID mutex=sceKernelCreateMutex("ACP JNI",0,0,NULL);
    if (mutex<0) return JNI_ERR;
    *b=(acp_jni_vita){mutex,-1,report,report_ctx};
    *out=(acp_jni_ops){b,thread_id,lock,unlock,fatal,check,code_ctx};
    return JNI_OK;
}
static int monitor_wait(void *ctx) {
    acp_jni_vita *b=ctx;int result=sceKernelWaitCond(b->monitor_changed,NULL);
    SceKernelMutexInfo info={.size=sizeof(info)};
    if(sceKernelGetMutexInfo(b->mutex,&info)<0)fatal(ctx,"JNI monitor mutex query failed");
    if(info.currentOwnerId!=sceKernelGetThreadId())lock(ctx);
    return result<0?-1:0;
}
static void monitor_wake(void *ctx){acp_jni_vita *b=ctx;if(sceKernelSignalCondAll(b->monitor_changed)<0)fatal(ctx,"JNI monitor wake failed");}
int acp_jni_vita_install_monitors(acp_jni_vita *b,acp_jni_runtime *r) {
    if(!b||b->mutex<0||b->monitor_changed>=0||!r)return JNI_ERR;
    int changed=sceKernelCreateCond("ACP JNI monitors",0,b->mutex,NULL);if(changed<0)return JNI_ERR;
    b->monitor_changed=changed;acp_jni_monitor_ops ops={b,monitor_wait,monitor_wake};
    int result=acp_jni_runtime_monitors(r,&ops);
    if(result!=JNI_OK&&sceKernelDeleteCond(changed)>=0)b->monitor_changed=-1;
    return result;
}
int acp_jni_vita_dispose(acp_jni_vita *b) {
    if (!b || b->mutex<0) return JNI_ERR;
    if(b->monitor_changed>=0){if(sceKernelDeleteCond(b->monitor_changed)<0)return JNI_ERR;b->monitor_changed=-1;}
    if(sceKernelDeleteMutex(b->mutex)<0)return JNI_ERR;
    b->mutex=-1;
    return JNI_OK;
}
