/* Local developer input/readback mailbox. Opt-in flag is never packaged or
 * deployed to a Vita. Commands enter the same Android queue as real touches. */
#include "acp_native.h"
#include <android/AInput.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <cmath>

static int enabled=-1;
static uint64_t next_poll;
static bool held;
static unsigned capture_request;
extern "C" { volatile unsigned acp_water_debug_mode; volatile unsigned acp_water_trace_request; }
extern "C" volatile unsigned acp_visibility_cache_mode;
extern "C" int acp_render_scopes_mode(unsigned);
#ifdef ACP_DEVELOPMENT_PROFILE
extern "C" volatile unsigned acp_callback_profile_request;
extern "C" volatile unsigned acp_eval_request;
#endif
extern "C" void acp_vita_test_pad(unsigned,unsigned);
extern "C" volatile unsigned acp_presented_frames;
struct Command {uint32_t magic,seq,kind,action;float x,y;};
static_assert(sizeof(Command)==24);
extern "C" unsigned acp_emulator_capture_request(void){
    return __atomic_exchange_n(&capture_request,0,__ATOMIC_ACQ_REL);
}
extern "C" void acp_emulator_io_poll(AInputQueue *queue){
    if(enabled<0){
        int fd=sceIoOpen(ACP_DATA_PATH "/emulator-api.flag",SCE_O_RDONLY,0);
        enabled=fd>=0;
        if(fd>=0){sceIoClose(fd);sceIoMkdir(ACP_DATA_PATH "/api",0777);acp_log("Local emulator input/readback API enabled");}
    }
    if(!enabled)return;
    uint64_t now=sceKernelGetProcessTimeWide();
    if(now<next_poll)return;
    next_poll=now+100000;
    int fd=sceIoOpen(ACP_DATA_PATH "/api/request.bin",SCE_O_RDONLY,0);
    if(fd<0)return;
    Command c{};int count=sceIoRead(fd,&c,sizeof(c));sceIoClose(fd);
    sceIoRemove(ACP_DATA_PATH "/api/request.bin");
    int result=-22;
    if(count==sizeof(c)&&c.magic==0x41504331&&c.seq){
        if(c.kind==2){__atomic_store_n(&capture_request,c.seq,__ATOMIC_RELEASE);result=0;}
#ifdef ACP_DEVELOPMENT_PROFILE
        else if(c.kind==13){__atomic_add_fetch(&acp_callback_profile_request,1,__ATOMIC_RELEASE);result=0;}
        else if(c.kind==14){__atomic_add_fetch(&acp_eval_request,1,__ATOMIC_RELEASE);result=0;}
#endif
        else if(c.kind==4&&c.action<=9){__atomic_store_n(&acp_water_debug_mode,c.action,__ATOMIC_RELEASE);result=0;}
        else if(c.kind==6){__atomic_store_n(&acp_water_trace_request,1,__ATOMIC_RELEASE);result=0;}
        else if(c.kind==7&&c.action<=1){__atomic_store_n(&acp_visibility_cache_mode,c.action,__ATOMIC_RELEASE);acp_log("Visibility comparison mode=%u frame=%u",c.action,acp_presented_frames);result=0;}
        else if(c.kind==8&&c.action<=1){result=acp_render_scopes_mode(c.action);acp_log("Render scopes comparison mode=%u frame=%u",c.action,acp_presented_frames);}
        else if((c.kind==3||c.kind==5)&&std::isfinite(c.x)&&std::isfinite(c.y)&&c.x>=0&&c.x<=255&&c.y>=0&&c.y<=255){
            unsigned sticks=c.kind==3 ? 0x80800000u|(unsigned)c.x|((unsigned)c.y<<8) : 0x8080u|((unsigned)c.x<<16)|((unsigned)c.y<<24);
            acp_vita_test_pad(c.action,sticks);result=0;
        }
        else if(c.kind==1&&c.action<=2&&std::isfinite(c.x)&&std::isfinite(c.y)&&
                c.x>=0&&c.x<960&&c.y>=0&&c.y<544&&((c.action==0&&!held)||(c.action!=0&&held))){
            inputEvent e{};e.source=AINPUT_SOURCE_TOUCHSCREEN;e.type=AINPUT_EVENT_TYPE_MOTION;
            e.motion_action=c.action;e.motion_ptrcount=1;e.motion_ptridx[0]=0;
            e.motion_x[0]=c.x;e.motion_y[0]=c.y;
            AInputEvent *event=AInputEvent_create(&e);
            if(event){AInputQueue_enqueueEvent(queue,event);held=c.action!=1;result=0;}
            else result=-12;
            acp_log("API touch seq=%u action=%u x=%.1f y=%.1f result=%d",c.seq,c.action,c.x,c.y,result);
        }
    }
    const uint32_t ack[]={0x41504331,c.seq,(uint32_t)result,acp_presented_frames};
    fd=sceIoOpen(ACP_DATA_PATH "/api/ack.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    if(fd>=0){sceIoWrite(fd,ack,sizeof(ack));sceIoClose(fd);}
}
