/* First-launch setup on the Vita. Runs in the game process before any game,
 * GPU or audio initialization: the CPU draws into two CDRAM scanout buffers.
 * When setup finishes, START relaunches the app so the game starts clean. */
#include "acp_setup.h"
#include "acp_setup_screen.h"
#include "acp_native.h"
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/vshbridge.h>
#include <stdio.h>
#include <string.h>
static uint32_t *frames[2];
static unsigned next,previous_buttons;
static uint64_t last_draw,last_tick;
static int last_percent=-1;
static const char *last_phase;
/* Local emulators (emulator-api.flag, never on a Vita) also take the test
 * mailbox's buttons, so drivers can press START and CIRCLE here. */
void acp_emulator_io_poll(void *);
extern unsigned acp_vita_test_buttons(void);
extern unsigned acp_emulator_capture_request(void);
static int emulator;
/* The emulator API's capture: the frame on screen, bottom row first like a GL readback. */
static void capture(void){
    unsigned seq=acp_emulator_capture_request();if(!seq)return;
    char path[128];snprintf(path,sizeof(path),ACP_SETUP_ROOT "/api/frame-%u.rgba",seq);
    SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return;
    const uint32_t *shown=frames[next^1];
    for(int y=543;y>=0;y--)sceIoWrite(fd,shown+y*960,960*4);
    sceIoClose(fd);
}
static unsigned edges(void){
    SceCtrlData pad;memset(&pad,0,sizeof(pad));
    unsigned buttons=sceCtrlPeekBufferPositive(0,&pad,1)<0?0:pad.buttons;
    if(emulator){acp_emulator_io_poll(NULL);buttons|=acp_vita_test_buttons();capture();}
    unsigned edge=buttons&~previous_buttons;previous_buttons=buttons;return edge;
}
static void present(const char *message,unsigned percent,int mode){
    /* Neither the frame on screen nor a pending flip is ever drawn into. */
    acp_setup_screen(frames[next],message,percent,mode);
    SceDisplayFrameBuf fb={sizeof(fb),frames[next],960,SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,960,544};
    if(sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME)<0||sceDisplayWaitSetFrameBuf()<0)
        acp_fatal("Setup display failed");
    next^=1;
}
static int progress(const char *phase,uint64_t done,uint64_t total,void *unused){
    (void)unused;uint64_t now=sceKernelGetProcessTimeWide();
    int percent=total?(int)(done*100/total):0,changed=phase!=last_phase;
    if(changed||(percent!=last_percent&&now-last_draw>=100000)){
        present(phase,(unsigned)percent,1);last_percent=percent;last_draw=now;
        if(changed){last_phase=phase;acp_log("Setup: %s",phase);}
    }
    if(now-last_tick>=1000000){sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);last_tick=now;}
    return (edges()&SCE_CTRL_CIRCLE)!=0;
}
static int exists(const char *path){SceIoStat st;return sceIoGetstat(path,&st)>=0;}
void acp_setup_on_vita(void){
    const char *prerequisite=NULL;int module[2]={0};
    emulator=exists(ACP_SETUP_ROOT "/emulator-api.flag");
    if(!emulator&&_vshKernelSearchModuleByName("kubridge",module)<0)
        prerequisite="kubridge is not running. Install kubridge.skprx under *KERNEL in ur0:tai/config.txt, then restart your Vita.";
    else if(!exists("ur0:data/libshacccg.suprx")&&!exists("ur0:data/external/libshacccg.suprx"))
        prerequisite="The shader compiler is missing. Run ShaRKF00D to extract libshacccg.suprx, then start the game again.";
    if(!prerequisite&&acp_setup_ready(ACP_SETUP_ROOT))return;
    acp_log("Setup: %s",prerequisite?prerequisite:"game files not prepared yet");
    uint32_t *memory=NULL;
    SceUID block=sceKernelAllocMemBlock("ACP setup screens",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,4u<<20,NULL);
    if(block<0||sceKernelGetMemBlockBase(block,(void **)&memory)<0)acp_fatal("Setup cannot allocate its screen");
    frames[0]=memory;frames[1]=memory+(2u<<20)/sizeof(*memory);
    acp_setup_screen_load("app0:sce_sys/pic0.png");
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    previous_buttons=~0u;
    acp_setup_result result;memset(&result,0,sizeof(result));
    int complete=0,drawn=-1;char shown[256]="";
    for(;;){
        int mode=prerequisite?3:complete?2:0;
        const char *message=prerequisite?prerequisite:result.message;
        if(mode!=drawn||strcmp(shown,message)){
            present(message,0,mode);present(message,0,mode);   /* both buffers */
            drawn=mode;snprintf(shown,sizeof(shown),"%s",message);
        }
        unsigned edge=edges();
        if(!complete&&(edge&SCE_CTRL_CIRCLE)){acp_log("Setup: closed by the player");sceKernelExitProcess(0);}
        if(!prerequisite&&(edge&SCE_CTRL_START)){
            if(complete){
                acp_log("Setup: relaunching the game");acp_log_flush();
                int rc=sceAppMgrLoadExec("app0:eboot.bin",NULL,NULL);
                snprintf(result.message,sizeof(result.message),"Close the game and start it again to play. (%08X)",(unsigned)rc);
            }else if(!acp_setup_find(ACP_SETUP_ROOT,&result)){
                uint64_t total=0,free_bytes=0,need=acp_setup_space(ACP_SETUP_ROOT,&result);
                if(sceAppMgrGetDevInfo("ux0:",&total,&free_bytes)<0)
                    snprintf(result.message,sizeof(result.message),"Cannot check the free space on ux0:. Start the game again.");
                else if(free_bytes<need)
                    snprintf(result.message,sizeof(result.message),"Free %u MB more on ux0:, then press START.",
                             (unsigned)((need-free_bytes+(1u<<20)-1)>>20));
                else{
                    /* Setup is loading work: the loading clock (444 MHz) only while it runs. */
                    int clock=scePowerGetArmClockFrequency();scePowerSetArmClockFrequency(444);
                    last_phase=NULL;last_percent=-1;last_draw=last_tick=0;
                    uint64_t begin=sceKernelGetProcessTimeWide();
                    int rc=acp_setup_run(ACP_SETUP_ROOT,&result,progress,NULL);
                    scePowerSetArmClockFrequency(clock);
                    complete=rc==0;
                    acp_log("Setup: result %d, %u built, %u reused, %llu s: %s",rc,result.completed,result.reused,
                            (unsigned long long)((sceKernelGetProcessTimeWide()-begin)/1000000),result.message);
                    acp_log_flush();
                }
                drawn=-1;
            }else drawn=-1;
            previous_buttons=~0u;
        }
        sceDisplayWaitVblankStart();
    }
}
