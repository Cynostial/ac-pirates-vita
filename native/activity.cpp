#include "acp_native.h"
#include <falso_ndk/FalsoNDK.h>
#include <stddef.h>
#include <string.h>
extern "C" int acp_graphics_install(void *);
extern "C" int ASensorManager_destroyEventQueue(ASensorManager *,ASensorEventQueue *);
static ANativeActivity activity{};
static ANativeActivityCallbacks callbacks{};
static int window_token,finished;
extern "C" void acp_game_memory_report(void);
static void finish(ANativeActivity *){acp_log("ANativeActivity_finish requested by %08x",(unsigned)(uintptr_t)__builtin_return_address(0));acp_game_memory_report();acp_log_flush();__atomic_store_n(&finished,1,__ATOMIC_RELEASE);}
static int width(ANativeWindow *){return 960;}
static int height(ANativeWindow *){return 544;}
static int format(ANativeWindow *){return 1;}
static int geometry(ANativeWindow *w,int x,int y,int f){
    return w==(ANativeWindow*)&window_token && (!x||x==960) && (!y||y==544) && (!f||f==1)?0:-22;
}
static int sdk(AConfiguration *){return 19;}
static void language(AConfiguration *,char *p){p[0]='e';p[1]='n';}
static void country(AConfiguration *,char *p){p[0]='U';p[1]='S';}
static int meta(const AInputEvent *){return 0;}
static float pressure(const AInputEvent *e,size_t index){
    return index<AMotionEvent_getPointerCount(e)&&((AMotionEvent_getAction(e)&255)!=AMOTION_EVENT_ACTION_UP)?1.0f:0.0f;
}
extern "C" int acp_activity_finished(void){return __atomic_load_n(&finished,__ATOMIC_ACQUIRE);}
extern "C" int acp_window_prepare(void){return acp_graphics_install(&window_token);}
extern "C" int acp_start_activity(void *vm,void *env,void *clazz,void *assets) {
    static_assert(sizeof(ANativeActivity)==40);
    static_assert(offsetof(ANativeActivity,instance)==28);
    static_assert(sizeof(ANativeActivityCallbacks)==64);
    activity.callbacks=&callbacks;activity.vm=vm;activity.env=env;activity.clazz=clazz;
    activity.internalDataPath="ux0:data/acpirates/userdata";
    activity.externalDataPath="ux0:data/acpirates/userdata/files";
    activity.obbPath="ux0:data/acpirates";activity.sdkVersion=19;
    activity.assetManager=(AAssetManager*)assets;
    auto create=(void (*)(ANativeActivity*,void*,size_t))acp_dlsym(nullptr,"ANativeActivity_onCreate");
    if(!create)return -1;
    acp_log("Entering ANativeActivity_onCreate");
    create(&activity,nullptr,0);
    if(!callbacks.onStart||!callbacks.onResume||!callbacks.onNativeWindowCreated||!callbacks.onInputQueueCreated)
        acp_fatal("NativeActivity did not install its required lifecycle callbacks");
    AInputQueue *queue=AInputQueue_create();
    if(!queue)return -1;
    callbacks.onStart(&activity);
    callbacks.onResume(&activity);
    callbacks.onInputQueueCreated(&activity,queue);
    callbacks.onNativeWindowCreated(&activity,(ANativeWindow*)&window_token);
    if(callbacks.onWindowFocusChanged)callbacks.onWindowFocusChanged(&activity,1);
    acp_log("NativeActivity resumed; window and input queue delivered");
    return 0;
}
#define B(n,f) {n,(uintptr_t)&f}
extern "C" uintptr_t acp_ndk_resolve(const char *name) {
    static const AcpSymbol symbols[]={
        B("ANativeActivity_finish",finish),B("ANativeWindow_getWidth",width),
        B("ANativeWindow_getHeight",height),B("ANativeWindow_getFormat",format),
        B("ANativeWindow_setBuffersGeometry",geometry),B("AConfiguration_getSdkVersion",sdk),
        B("AConfiguration_getLanguage",language),B("AConfiguration_getCountry",country),
        B("AKeyEvent_getMetaState",meta),B("AMotionEvent_getPressure",pressure),
        B("pipe",fndk_pipe),
#include "ndk_bindings.inc"
    };
    for(const auto &s:symbols)if(!strcmp(name,s.name))return s.address;
    return 0;
}
