/* Vita3K 5844e21f io.cpp mutates IOState::next_fd/std_files without a lock;
 * SceIofilemgr's exports do not add one. Serialize guest file calls only in
 * the explicitly opted-in local emulator. Real Vita I/O stays concurrent.
 * This addresses the observed EBADFD failure; it does not establish the
 * cause of every earlier emulator exit or damaged save. */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "acp_native.h"
static SceKernelLwMutexWork gate;
static int guarded;
extern SceUID __real_sceIoOpen(const char *,int,SceMode);
extern int __real_sceIoClose(SceUID);
int acp_emulator_io_guard_init(void){
    int fd=__real_sceIoOpen(ACP_DATA_PATH "/emulator-api.flag",SCE_O_RDONLY,0);
    if(fd<0)return 0;
    __real_sceIoClose(fd);
    if(sceKernelCreateLwMutex(&gate,"ACP emulator IO",0,0,NULL)<0)sceKernelExitProcess(1);
    guarded=1;return 1;
}
static void enter(void){if(sceKernelLockLwMutex(&gate,1,NULL)<0)sceKernelExitProcess(1);}
static void leave(void){if(sceKernelUnlockLwMutex(&gate,1)<0)sceKernelExitProcess(1);}
#define IO_WRAP(type,name,params,args) \
    extern type __real_##name params; \
    type __wrap_##name params { \
        if(!guarded)return __real_##name args; \
        enter();type result=__real_##name args;leave();return result; \
    }
IO_WRAP(SceUID,sceIoOpen,(const char *p,int f,SceMode m),(p,f,m))
IO_WRAP(int,sceIoClose,(SceUID fd),(fd))
IO_WRAP(SceSSize,sceIoRead,(SceUID fd,void *p,SceSize n),(fd,p,n))
IO_WRAP(SceSSize,sceIoWrite,(SceUID fd,const void *p,SceSize n),(fd,p,n))
IO_WRAP(int,sceIoPread,(SceUID fd,void *p,SceSize n,SceOff o),(fd,p,n,o))
IO_WRAP(int,sceIoPwrite,(SceUID fd,const void *p,SceSize n,SceOff o),(fd,p,n,o))
IO_WRAP(SceOff,sceIoLseek,(SceUID fd,SceOff o,int w),(fd,o,w))
IO_WRAP(long,sceIoLseek32,(SceUID fd,long o,int w),(fd,o,w))
IO_WRAP(int,sceIoSyncByFd,(SceUID fd,int f),(fd,f))
IO_WRAP(SceUID,sceIoDopen,(const char *p),(p))
IO_WRAP(int,sceIoDclose,(SceUID fd),(fd))
IO_WRAP(int,sceIoDread,(SceUID fd,SceIoDirent *p),(fd,p))
IO_WRAP(int,sceIoGetstat,(const char *p,SceIoStat *s),(p,s))
IO_WRAP(int,sceIoGetstatByFd,(SceUID fd,SceIoStat *s),(fd,s))
IO_WRAP(int,sceIoChstat,(const char *p,SceIoStat *s,int b),(p,s,b))
IO_WRAP(int,sceIoChstatByFd,(SceUID fd,const SceIoStat *s,unsigned b),(fd,s,b))
IO_WRAP(int,sceIoMkdir,(const char *p,SceMode m),(p,m))
IO_WRAP(int,sceIoRmdir,(const char *p),(p))
IO_WRAP(int,sceIoRemove,(const char *p),(p))
IO_WRAP(int,sceIoRename,(const char *p,const char *q),(p,q))
IO_WRAP(int,sceIoDevctl,(const char *p,unsigned c,void *in,unsigned ni,void *out,unsigned no),(p,c,in,ni,out,no))
