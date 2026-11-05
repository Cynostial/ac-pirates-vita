/* Keep the Lua mutator and collector on separate user cores. Their original
 * synchronization is unchanged; affinity avoids repeatedly discarding each
 * thread's private L1 working set when the collector wakes and sleeps. */
#include "acp_native.h"
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
static int worker=-1;
static int game=-1;
static void keep_core(int *owner,int mask,const char *name){
    /* Only the first thread for each role changes affinity. In particular,
     * calling this from a replacement thread must not move unrelated workers. */
    if(__atomic_load_n(owner,__ATOMIC_ACQUIRE)>=0)return;
    int id=sceKernelGetThreadId(),unset=-1;
    if(id<0||!__atomic_compare_exchange_n(owner,&unset,id,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
    int previous=sceKernelGetThreadCpuAffinityMask(id);
    int result=previous<0?previous:sceKernelChangeThreadCpuAffinityMask(id,mask);
    int actual=sceKernelGetThreadCpuAffinityMask(id);
    acp_log("Thread placement: %s id=%x previous=%x requested=%x actual=%x result=%x",
        name,id,previous,mask,actual,result);
}
void acp_game_thread_record(void){
    keep_core(&game,SCE_KERNEL_CPU_MASK_USER_1,"game");
}
void acp_gc_thread_record(void){
    keep_core(&worker,SCE_KERNEL_CPU_MASK_USER_2,"Lua-GC");
}
void acp_thread_timing_report(void){
    extern void acp_asset_io_report(void);acp_asset_io_report();
    static SceKernelThreadInfo previous[2];
    static int ids[2]={-1,-1};
    int current[2]={sceKernelGetThreadId(),__atomic_load_n(&worker,__ATOMIC_ACQUIRE)};
    for(unsigned i=0;i<2;i++){
        if(current[i]<0)continue;
        SceKernelThreadInfo info={.size=sizeof(info)};
        if(sceKernelGetThreadInfo(current[i],&info)<0)continue;
        if(ids[i]==current[i])acp_log("Thread window: %s affinity=%x priority=%d cpu=%d run-ticks=%llu preemptions=%u migrations=%d",
            i?"Lua-GC":"game",info.currentCpuAffinityMask,info.currentPriority,info.lastExecutedCpuId,
            (unsigned long long)(info.runClocks-previous[i].runClocks),
            info.threadPreemptCount-previous[i].threadPreemptCount,info.changeCpuCount-previous[i].changeCpuCount);
        previous[i]=info;ids[i]=current[i];
    }
}
