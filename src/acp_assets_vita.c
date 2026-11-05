#include "acp_assets.h"
#include "acp_native.h"
#include "acp_platform.h"
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <string.h>
static acp_assets manager;
static SceUID mutex=-1;
static unsigned opens,misses,reads,seeks,io_us;
static void lock(void *ctx){(void)ctx;if(sceKernelLockMutex(mutex,1,NULL)<0){sceKernelExitProcess(1);abort();}}
static void unlock(void *ctx){(void)ctx;if(sceKernelUnlockMutex(mutex,1)<0){sceKernelExitProcess(1);abort();}}
void acp_asset_io_report(void){
    if(mutex<0)return;
    lock(NULL);
    unsigned o=opens,m=misses,r=reads,s=seeks,t=io_us;
    opens=misses=reads=seeks=io_us=0;
    unlock(NULL);
    if(o||r)acp_log("APK asset IO: opens=%u missing=%u reads=%u seeks=%u time=%u us",o,m,r,s,t);
}
extern int acp_apk_stream_init(void);
extern int acp_apk_stream_open(const char *,uint64_t *);
extern int acp_apk_stream_read(int,void *,unsigned);
extern int64_t acp_apk_stream_seek(int,int64_t);
extern int acp_apk_stream_close(int);
static struct {int active,packed,fd;} files[ACP_ASSET_HANDLES];
static int open_asset(void *ctx,const char *path,uint64_t *size){
    (void)ctx;unsigned begin=sceKernelGetProcessTimeLow();opens++;
    unsigned slot=0;while(slot<ACP_ASSET_HANDLES&&files[slot].active)slot++;
    if(slot==ACP_ASSET_HANDLES)return -1;
    int fd=acp_apk_stream_open(path,size),packed=fd>=0;
    if(fd==-2){
        fd=sceIoOpen(path,SCE_O_RDONLY,0);
        if(fd>=0){SceOff n=sceIoLseek(fd,0,SCE_SEEK_END);
            if(n<0||sceIoLseek(fd,0,SCE_SEEK_SET)<0){sceIoClose(fd);fd=-1;}
            else *size=(uint64_t)n;
        }
    }
    io_us+=sceKernelGetProcessTimeLow()-begin;
    if(fd<0){misses++;return -1;}
    files[slot].active=1;files[slot].packed=packed;files[slot].fd=fd;return (int)slot;
}
static int read_asset(void *ctx,int slot,void *out,unsigned n){
    (void)ctx;unsigned begin=sceKernelGetProcessTimeLow();reads++;
    int result=files[slot].packed?acp_apk_stream_read(files[slot].fd,out,n):sceIoRead(files[slot].fd,out,n);
    io_us+=sceKernelGetProcessTimeLow()-begin;return result;
}
static int64_t seek_asset(void *ctx,int slot,int64_t pos){
    (void)ctx;seeks++;
    return files[slot].packed?acp_apk_stream_seek(files[slot].fd,pos):sceIoLseek(files[slot].fd,pos,SCE_SEEK_SET);
}
static int close_asset(void *ctx,int slot){
    (void)ctx;int r=files[slot].packed?acp_apk_stream_close(files[slot].fd):sceIoClose(files[slot].fd);
    if(!r)memset(files+slot,0,sizeof(files[slot]));
    return r;
}
int acp_assets_vita_init(void){
    if(mutex>=0)return -1;
    mutex=sceKernelCreateMutex("ACP assets",0,0,NULL);if(mutex<0)return -1;
    acp_apk_stream_init();
    acp_asset_io io={NULL,lock,unlock,open_asset,read_asset,seek_asset,close_asset};
    if(acp_assets_init(&manager,ACP_DATA "assets/",&io)){
        sceKernelDeleteMutex(mutex);mutex=-1;return -1;
    }
    return 0;
}
void *acp_assets_vita_manager(void){return mutex>=0?&manager:NULL;}
static void *AAssetManager_fromJava(JNIEnv *e,jobject obj){
    return acp_jni_runtime_data(e,obj,"android/content/res/AssetManager");
}
static void *AAssetManager_open(void *m,const char *name,int mode){
    if(m!=&manager||mutex<0||mode<0||mode>3)return NULL;
    return acp_asset_open(&manager,name);
}
static int AAsset_read(void *a,void *out,size_t n){return acp_asset_read(&manager,a,out,n);}
static int32_t AAsset_seek(void *a,int32_t offset,int whence){return acp_asset_seek(&manager,a,offset,whence);}
static int32_t AAsset_getLength(void *a){return acp_asset_length(&manager,a,0);}
static int32_t AAsset_getRemainingLength(void *a){return acp_asset_length(&manager,a,1);}
static void AAsset_close(void *a){(void)acp_asset_close(&manager,a);}
uintptr_t acp_assets_resolve(const char *name){
    if(!name)return 0;
#define BIND(n) if(!strcmp(name,#n))return (uintptr_t)&n
    BIND(AAssetManager_fromJava);BIND(AAssetManager_open);BIND(AAsset_read);
    BIND(AAsset_seek);BIND(AAsset_getLength);BIND(AAsset_getRemainingLength);BIND(AAsset_close);
#undef BIND
    return 0;
}
