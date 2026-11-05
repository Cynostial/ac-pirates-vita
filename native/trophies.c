/* PS Vita trophies for the game's achievements. The trophy pack is
 * sce_sys/trophy/ACPR00001_00/TROPHY.TRP (tools/build_trophies.py): trophy 0
 * is the platinum, 1..99 the achievements in AchievementsManager order.
 * Homebrew packs are unsigned; the system accepts them only with
 * the NoTrpDrm plugin, otherwise the context cannot be created
 * and trophies stay unavailable. Runs only on the online worker thread
 * (online.c); the game thread draws the setup dialog (acp_trophy_dialog_frame). */
#include "acp_native.h"
#include <psp2/common_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <string.h>

/* vitasdk has the stubs but no trophy header. */
typedef struct {int sdkVersion;SceCommonDialogParam commonParam;int context;int options;uint8_t reserved[128];} SceNpTrophySetupDialogParam;
typedef struct {uint32_t flag[4];} SceNpTrophyFlagArray;
int sceNpTrophyInit(void *);
int sceNpTrophyCreateContext(int *,const void *,const void *,uint64_t);
int sceNpTrophySetupDialogInit(SceNpTrophySetupDialogParam *);
SceCommonDialogStatus sceNpTrophySetupDialogGetStatus(void);
int sceNpTrophySetupDialogTerm(void);
int sceNpTrophyCreateHandle(int *);
int sceNpTrophyDestroyHandle(int);
int sceNpTrophyUnlockTrophy(int,int,int,int *);
int sceNpTrophyGetTrophyUnlockState(int,int,SceNpTrophyFlagArray *,uint32_t *);

#define TROPHY_ALREADY_UNLOCKED 0x8055160Fu
#define COMMON_DIALOG_BUSY 0x80020401u
/* SceNpCommunicationId: "ACPR00001", terminator, num 0 (the _00 folder). */
static const char COMM_ID[12]="ACPR00001";
/* SceNpCommunicationSignature: the header every homebrew pack passes; the
 * rest of the signature check is what NoTrpDrm disables. */
static const unsigned char SIGNATURE[160]={0xb9,0xdd,0xe1,0x3b,0x01,0x00};
static int context=-1;
static SceNpTrophyFlagArray unlocked;
static volatile int dialog;

int acp_trophy_dialog_frame(void){return dialog;}

static int setup(void){
    SceNpTrophySetupDialogParam param;
    int r=-1;
    for(int tries=0;tries<300;tries++){   /* another common dialog may be open */
        memset(&param,0,sizeof(param));
        _sceCommonDialogSetMagicNumber(&param.commonParam);
        param.sdkVersion=PSP2_SDK_VERSION;param.context=context;
        dialog=1;
        r=sceNpTrophySetupDialogInit(&param);
        if((unsigned)r!=COMMON_DIALOG_BUSY)break;
        dialog=0;sceKernelDelayThread(100000);
    }
    if(r<0){dialog=0;return r;}
    for(int i=0;i<6000&&sceNpTrophySetupDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING;i++)sceKernelDelayThread(10000);
    sceNpTrophySetupDialogTerm();
    dialog=0;
    return 0;
}
/* 1 ready, -1 unavailable. */
int acp_trophy_start(void){
    int r=sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
    if(r>=0)r=sceNpTrophyInit(NULL);
    if(r>=0)r=sceNpTrophyCreateContext(&context,COMM_ID,SIGNATURE,0);
    if(r<0){acp_log("Trophies: unavailable (0x%08x; NoTrpDrm plugin not loaded?)",(unsigned)r);context=-1;return -1;}
    r=setup();
    if(r<0){acp_log("Trophies: setup failed 0x%08x",(unsigned)r);context=-1;return -1;}
    int handle=-1;uint32_t count=0;
    if(sceNpTrophyCreateHandle(&handle)>=0){
        sceNpTrophyGetTrophyUnlockState(context,handle,&unlocked,&count);
        sceNpTrophyDestroyHandle(handle);
    }
    unsigned have=0;
    for(int i=0;i<4;i++)have+=(unsigned)__builtin_popcount(unlocked.flag[i]);
    acp_log("Trophies: ready, %u of %u unlocked",have,(unsigned)count);
    return 1;
}
int acp_trophy_unlock(int id){
    if(context<0||id<1||id>127)return -1;
    uint32_t bit=1u<<(id&31);
    if(unlocked.flag[id>>5]&bit)return 0;
    int handle=-1,platinum=-1,r=sceNpTrophyCreateHandle(&handle);
    if(r>=0){r=sceNpTrophyUnlockTrophy(context,handle,id,&platinum);sceNpTrophyDestroyHandle(handle);}
    if(r>=0||(unsigned)r==TROPHY_ALREADY_UNLOCKED){
        unlocked.flag[id>>5]|=bit;
        if(r>=0)acp_log("Trophies: unlocked %d%s",id,platinum>=0?" and the platinum":"");
        return 0;
    }
    acp_log("Trophies: unlock %d failed 0x%08x",id,(unsigned)r);
    return r;
}
