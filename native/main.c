#include "acp_native.h"
#include "acp_platform.h"
#include "acp_jni_vita.h"
#include "acp_assets.h"
#include "acp_graphics.h"
#include "acp_manifest.h"
#include "startup_cache.h"
#include "acp_popup.h"
#include "acp_sdk.h"
#include "obb_id.h"
#include "../installer/acp_setup.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/sysmodule.h>
#include <stdlib.h>
#include <unistd.h>
unsigned int _newlib_heap_size_user=32u*1024*1024;
unsigned int sceUserMainThreadStackSize=1024*1024;
int _sceAppUtilAppId=0;
static acp_jni_registry registry;
static acp_jni_vita backend;
static acp_platform platform;
static acp_manifest manifest;
extern int acp_load_game(void);
extern void acp_preferences_load(void);
extern void acp_game_initialize(void);
extern int acp_code_check(void *,const void *);
extern int acp_music_configure(acp_jni_runtime *);
extern int acp_services_configure(acp_platform *,acp_jni_registry *);
extern int acp_services_values(JNIEnv *);
extern int acp_cache_vita_configure(acp_platform *);
extern volatile unsigned acp_presented_frames;
static void jni_failure(void *ctx,const char *s){(void)ctx;acp_log("JNI failure: %s",s);}
static void jni_exception(void *ctx,const char *s){(void)ctx;acp_log("JNI exception: %s",s);}
static void status(const char *s){acp_log("%s",s);}
/* The game thread never returns to Java, where Android would throw a pending
 * exception and so end it. Release Android also lets later JNI calls run;
 * this runtime refuses them while one is pending. The offline social SDK
 * leaves one behind at the main menu, which silently dropped every later
 * music change. Report and clear it at each presented frame. */
static JavaVM *frame_vm;
/* Vita3K cannot sample GXM ETC1, so VitaGL transcodes ETC1 to cached BC1
 * there (local emulators carry emulator-api.flag). The Vita samples the
 * game's original ETC1 blocks directly. */
int acp_etc1_transcode(void){
    static int emulator=-1;
    if(emulator<0){
        SceUID fd=sceIoOpen(ACP_DATA_PATH "/emulator-api.flag",SCE_O_RDONLY,0);
        emulator=fd>=0;if(fd>=0)sceIoClose(fd);
        acp_log("Textures: %s",emulator?"ETC1 transcoded to cached BC1 (emulator)":"native GXM ETC1");
    }
    return emulator;
}
void acp_jni_frame_boundary(void){
    JNIEnv *env=NULL;
    if(!frame_vm||(*frame_vm)->GetEnv(frame_vm,(void **)&env,JNI_VERSION_1_6)!=JNI_OK||!env)return;
    if(!(*env)->ExceptionCheck(env))return;
    static unsigned cleared;
    if(++cleared<=8||!(cleared%256)){acp_log("JNI: clearing stale exception %u at frame end",cleared);(*env)->ExceptionDescribe(env);}
    else (*env)->ExceptionClear(env);
}
static void shader(unsigned id,int program,int success,const char *message){
    if(!success)acp_log("%s %u compile/link failed: %s",program?"Program":"Shader",id,message);
}
int main(void){
#ifdef ACP_DEVELOPMENT_PROFILE
    extern int acp_emulator_io_guard_init(void);
    acp_emulator_io_guard_init();
#endif
    sceIoMkdir("ux0:data/acpirates",0777);
    int log=sceIoOpen("ux0:data/acpirates/runtime.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(log>=0)sceIoClose(log);
    acp_log_init();
    acp_log("AC Pirates 2.9.1 Vita native port 00.81 starting");
    acp_log("System clocks at launch: CPU=%d bus=%d GPU=%d GPU-xbar=%d MHz",
            scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),
            scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    /* First launch: build ux0:data/acpirates from the player's APK and OBB. */
    acp_setup_on_vita();
    acp_loading_boost(ACP_LOADING_LAUNCH,1);
    sceSysmoduleLoadModule(SCE_SYSMODULE_FIOS2);
    sceIoMkdir(ACP_DATA "userdata",0777);sceIoMkdir(ACP_DATA "userdata/files",0777);sceIoMkdir(ACP_DATA "userdata/cache",0777);
    chdir(ACP_DATA "userdata");acp_stdio_init();acp_libc_init();acp_preferences_load();
    acp_save_backup();
    SceIoStat st;
    /* The original archive, or its pre-decrypted copy (build_plain_obb.py). */
    if(sceIoGetstat(ACP_DATA ACP_OBB,&st)<0||(st.st_size!=ACP_OBB_ORIGINAL_BYTES&&st.st_size!=ACP_OBB_PLAIN_BYTES))
        acp_fatal("Matching main.253 OBB is missing or incomplete");
    acp_obb_bytes=(unsigned)st.st_size;
    acp_log("OBB: %s archive, %u bytes",acp_obb_bytes==ACP_OBB_PLAIN_BYTES?"pre-decrypted":"original encrypted",acp_obb_bytes);
    acp_startup_cache_init();
    acp_graphics_set_log(shader);
    acp_graphics_set_status(status);
    if(acp_window_prepare())acp_fatal("Window initialization failed");
    acp_load_game();
    acp_log("Initializing Android services");
    if(acp_assets_vita_init())acp_fatal("Asset manager initialization failed");
    acp_jni_registry_init(&registry);
    if(acp_platform_declare_natives(&registry))acp_fatal("Native method declarations failed");
    acp_jni_ops ops;
    if(acp_jni_vita_ops(&backend,&ops,acp_code_check,NULL,jni_failure,NULL))acp_fatal("JNI mutex initialization failed");
    size_t bytes=acp_jni_runtime_size();void *storage=calloc(1,bytes);
    acp_jni_runtime *runtime=acp_jni_runtime_create(storage,bytes,&registry,&ops);
    if(!runtime||acp_jni_runtime_exception_reporter(runtime,jni_exception,NULL)||
       acp_jni_runtime_trace_exceptions(runtime,1)||
       acp_jni_vita_install_monitors(&backend,runtime)||
       acp_platform_configure(&platform,runtime,ACP_DATA "platform.args",1))acp_fatal("Android platform initialization failed");
    if(acp_music_configure(runtime))acp_fatal("Streamed music initialization failed");
    if(acp_services_configure(&platform,&registry))acp_fatal("Device service initialization failed");
    if(acp_popup_vita_configure(runtime))acp_fatal("Popup service initialization failed");
    if(acp_sdk_vita_configure(&platform,&registry))acp_fatal("Local SDK service initialization failed");
    if(acp_cache_vita_configure(&platform))acp_fatal("Asset cache service initialization failed");
    if(acp_manifest_configure(&manifest,runtime,"app0:manifest.names"))
        acp_fatal("APK manifest service initialization failed");
    acp_log("APK manifest index: %u names, %u bytes",manifest.count,manifest.bytes);
    manifest.status=status;platform.status=status;
    platform.asset_manager=acp_assets_vita_manager();
    JavaVM *vm=acp_jni_runtime_vm(runtime);JNIEnv *env=NULL;frame_vm=vm;
    if((*vm)->AttachCurrentThread(vm,&env,NULL))acp_fatal("Main JNI attachment failed");
    if(acp_services_values(env))acp_fatal("Device field initialization failed");
    jobject local=acp_platform_activity(env,&platform),activity=(*env)->NewGlobalRef(env,local);
    (*env)->DeleteLocalRef(env,local);if(!activity)acp_fatal("Activity allocation failed");
    acp_game_initialize();
    acp_log("Starting NativeActivity");
    if(acp_start_activity(vm,env,activity,platform.asset_manager))acp_fatal("NativeActivity startup failed");
    unsigned ticks=0;
    while(!acp_activity_finished()){
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);sceKernelDelayThread(100000);
        if(++ticks==300){ticks=0;acp_log("Game activity alive: %u frames submitted",acp_presented_frames);}
    }
    sceKernelExitProcess(0);return 0;
}
