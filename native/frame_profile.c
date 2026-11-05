/* Sample full Lua frames, pacing and post-update resource work. No allocation. */
#include "acp_native.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <stdint.h>
#include <pthread.h>
#include "acp_frame_wait.h"
extern volatile unsigned acp_presented_frames;
#if defined(ACP_DEVELOPMENT_PROFILE) || defined(ACP_UPDATE_SAMPLES)
extern int acp_method_frame(int);
extern void acp_method_report(void);
#endif
typedef struct {uint64_t begin,total;unsigned count,maximum;int active;} Phase;
static Phase phases[3];
static pthread_mutex_t stats_lock=PTHREAD_MUTEX_INITIALIZER;
static int late_pacing=-1;
static acp_wait_state waits;
static unsigned wait_allowance;
static void (*start_gc)(void *),(*stop_gc)(void *);
static uint64_t now(void *ctx){(void)ctx;return sceKernelGetProcessTimeWide();}
static void delay(void *ctx,unsigned us){(void)ctx;if(sceKernelDelayThread(us)<0)acp_fatal("Frame wait failed");}
void acp_frame_wait_install(void (*start)(void *),void (*stop)(void *)){start_gc=start;stop_gc=stop;}
void acp_frame_wait(void *state,unsigned us,int collect){
    const acp_wait_ops ops={now,delay,start_gc,stop_gc};
    acp_wait_state sample={.allowance=__atomic_load_n(&wait_allowance,__ATOMIC_RELAXED)};
    acp_wait_run(&sample,&ops,state,us,collect);
    __atomic_store_n(&wait_allowance,sample.allowance,__ATOMIC_RELAXED);
    pthread_mutex_lock(&stats_lock);
    waits.requests+=sample.requests;waits.collections+=sample.collections;
    waits.requested+=sample.requested;waits.elapsed+=sample.elapsed;waits.stop_time+=sample.stop_time;
    if(sample.max_stop>waits.max_stop)waits.max_stop=sample.max_stop;
    if(sample.max_overrun>waits.max_overrun)waits.max_overrun=sample.max_overrun;
    pthread_mutex_unlock(&stats_lock);
}
static int exists(const char *path){
    int fd=sceIoOpen(path,SCE_O_RDONLY,0);if(fd<0)return 0;sceIoClose(fd);return 1;
}
int acp_frame_phase(int index,int begin){
#ifdef ACP_DEVELOPMENT_PROFILE
    extern void acp_callback_profile_frame(int);
    if(index==0)acp_callback_profile_frame(begin);
#endif
    if(index==-1){
        if(late_pacing<0)late_pacing=!(exists(ACP_DATA_PATH "/emulator-api.flag")&&
                                      exists(ACP_DATA_PATH "/pacing-original.flag"));
        return late_pacing;
    }
    if(index<0||index>=3)return 0;
    /* This is the real Lua update thread, after NativeActivity startup. */
    if(index==0&&begin){extern void acp_game_thread_record(void);acp_game_thread_record();}
#if defined(ACP_DEVELOPMENT_PROFILE) || defined(ACP_UPDATE_SAMPLES)
    int update_sample=index==0?acp_method_frame(begin):0;
#endif
    Phase *p=phases+index;
    if(begin){
        p->active=!(acp_presented_frames&15u);
        if(p->active)p->begin=sceKernelGetProcessTimeWide();
    }else if(p->active){
        uint64_t elapsed=sceKernelGetProcessTimeWide()-p->begin;
        p->active=0;
        unsigned us=elapsed>UINT32_MAX?UINT32_MAX:(unsigned)elapsed;
        pthread_mutex_lock(&stats_lock);
        p->total+=elapsed;p->count++;
        if(us>p->maximum)p->maximum=us;
        pthread_mutex_unlock(&stats_lock);
    }
#if defined(ACP_DEVELOPMENT_PROFILE) || defined(ACP_UPDATE_SAMPLES)
    return update_sample;
#else
    return 0;
#endif
}
void acp_frame_profile_report(void){
    extern void acp_mesh_transform_report(void);acp_mesh_transform_report();
    extern void acp_live_uniforms_report(void);acp_live_uniforms_report();
#ifdef ACP_CLASS_COUNTERS
    extern void acp_class_lookup_report(void);acp_class_lookup_report();
#endif
#if defined(ACP_DEVELOPMENT_PROFILE) || defined(ACP_UPDATE_SAMPLES)
    acp_method_report();
#endif
    Phase report[3];acp_wait_state w;
    pthread_mutex_lock(&stats_lock);
    for(unsigned i=0;i<3;i++){
        report[i]=(Phase){.total=phases[i].total,.count=phases[i].count,.maximum=phases[i].maximum};
        phases[i].total=0;phases[i].count=phases[i].maximum=0;
    }
    w=waits;waits=(acp_wait_state){0};
    pthread_mutex_unlock(&stats_lock);
    acp_log("Lua frame phases (1/16): full=%u us n=%u max=%u pacing=%u us n=%u max=%u maintenance=%u us n=%u max=%u",
        report[0].count?(unsigned)(report[0].total/report[0].count):0,report[0].count,report[0].maximum,
        report[1].count?(unsigned)(report[1].total/report[1].count):0,report[1].count,report[1].maximum,
        report[2].count?(unsigned)(report[2].total/report[2].count):0,report[2].count,report[2].maximum);
    acp_log("Frame waits: n=%u requested=%u us actual=%u us GC-stop=%u us n=%u max=%u allowance=%u us max-overrun=%u us",
        w.requests,w.requests?(unsigned)(w.requested/w.requests):0,
        w.requests?(unsigned)(w.elapsed/w.requests):0,
        w.collections?(unsigned)(w.stop_time/w.collections):0,w.collections,
        w.max_stop,__atomic_load_n(&wait_allowance,__ATOMIC_RELAXED),w.max_overrun);
}
