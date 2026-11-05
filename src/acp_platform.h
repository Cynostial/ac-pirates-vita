#ifndef ACP_PLATFORM_H
#define ACP_PLATFORM_H
#include "acp_jni_runtime.h"
#define ACP_DATA "ux0:data/acpirates/"
#define ACP_OBB "main.253.com.ubisoft.assassin.pirates.obb"
extern unsigned acp_obb_bytes;
#define ACP_ACTIVITY "com/ubisoft/assassin/pirates/SparkActivity"
typedef struct {
    acp_jni_runtime *runtime;
    void *asset_manager; /* Set before threads attach; borrowed process-lifetime manager. */
    const char *keys[256],*values[256];
    unsigned pairs,obb_ready;
    void (*status)(const char *); /* Optional startup milestones, set before attach. */
    int (*cache_file)(void *,const char *,const char *,char *,size_t);
    void *cache_context;
    unsigned char strings[65536];
} acp_platform;
/* Fixed storage, immutable platform-argument map. Must run before attaching
 * any JNI thread. Archive readiness is set after bootstrap file verification. */
int acp_platform_configure(acp_platform *,acp_jni_runtime *,const char *args_path,int obb_ready);
int acp_platform_declare_natives(acp_jni_registry *);
jobject acp_platform_activity(JNIEnv *,acp_platform *);
#endif
