/* Sparse phase timings at four verified call sites. No Lua method wrappers
 * are installed in release builds. Original calls and results are preserved. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>
#ifdef ACP_DEVELOPMENT_PROFILE
extern int acp_method_register(const char *);
extern int acp_method_phase(int,int);
#endif
static uintptr_t (*originals[4])(uintptr_t,uintptr_t);
#ifdef ACP_DEVELOPMENT_PROFILE
static int ids[4];
#endif
extern volatile unsigned acp_presented_frames;
static struct {unsigned count,total,maximum;} timing[4];
static uintptr_t invoke(unsigned i,uintptr_t a,uintptr_t b){
#ifdef ACP_DEVELOPMENT_PROFILE
    int active=acp_method_phase(ids[i],1);
#endif
    int sample=!(acp_presented_frames&63u);
    unsigned begin=sample?sceKernelGetProcessTimeLow():0;
    uintptr_t result=originals[i](a,b);
    if(sample){
        unsigned elapsed=sceKernelGetProcessTimeLow()-begin;
        timing[i].count++;timing[i].total+=elapsed;
        if(elapsed>timing[i].maximum)timing[i].maximum=elapsed;
    }
#ifdef ACP_DEVELOPMENT_PROFILE
    if(active)acp_method_phase(ids[i],0);
#endif
    return result;
}
#define WRAPPER(n) static uintptr_t wrap##n(uintptr_t a,uintptr_t b){return invoke(n,a,b);}
WRAPPER(0) WRAPPER(1) WRAPPER(2) WRAPPER(3)
void acp_engine_timing_report(void){
    extern void acp_visibility_cache_report(void);acp_visibility_cache_report();
#ifdef ACP_DEVELOPMENT_PROFILE
    extern void acp_render_detail_report(void);acp_render_detail_report();
#endif
    static const char *names[]={"display-query","render-submit","present-call","collector-join"};
    for(unsigned i=0;i<4;i++){
        if(timing[i].count)acp_log("Engine phase (1/64): %s total=%u us n=%u max=%u us",
            names[i],timing[i].total,timing[i].count,timing[i].maximum);
        timing[i].count=timing[i].total=timing[i].maximum=0;
    }
}
int acp_engine_profile_install(so_module *m){
    static const struct {uint32_t site,target;uint8_t guard[4];const char *name;uintptr_t wrapper;int link;} hooks[]={
        {0x57a7f4,0x6aa201,{0x2f,0xf1,0x04,0xfd},"native:display-resolution",(uintptr_t)wrap0,1},
        {0x57a88e,0x7834a5,{0x08,0xf2,0x09,0xfe},"native:render",(uintptr_t)wrap1,1},
        {0x57a894,0x782f09,{0x08,0xf2,0x38,0xfb},"native:present",(uintptr_t)wrap2,1},
        {0x57a94e,0x6712e5,{0xf6,0xf0,0xc9,0xbc},"native:stop-gc",(uintptr_t)wrap3,0},
    };
    for(unsigned i=0;i<4;i++)if(memcmp((void *)(m->load_addr+hooks[i].site),hooks[i].guard,4))return -1;
    for(unsigned i=0;i<4;i++){
        uintptr_t site=m->load_addr+hooks[i].site,stub=(m->patch_head+3u)&~3u;
        if(stub<m->patch_base||stub+8>m->patch_base+m->patch_size)return -1;
        int32_t displacement=(int32_t)(stub-site-4);
        if(displacement<-(1<<24)||displacement>=(1<<24)||(displacement&1))return -1;
        uint32_t bits=(uint32_t)displacement,s=(bits>>24)&1;
        uint16_t branch[2]={(uint16_t)(0xf000|(s<<10)|((bits>>12)&1023)),
            (uint16_t)((hooks[i].link?0xd000:0x9000)|((1^((bits>>23)&1)^s)<<13)|
                ((1^((bits>>22)&1)^s)<<11)|((bits>>1)&2047))};
        uint8_t jump[8];if(acp_game_patch_build((uint32_t)hooks[i].wrapper,jump))return -1;
        if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,8)<0)return -1;
        kuKernelFlushCaches((void *)stub,8);
        originals[i]=(void *)(m->load_addr+hooks[i].target);
#ifdef ACP_DEVELOPMENT_PROFILE
        ids[i]=acp_method_register(hooks[i].name);
#endif
        if(kuKernelCpuUnrestrictedMemcpy((void *)site,branch,4)<0)return -1;
        m->patch_head=stub+8;
    }
#ifdef ACP_DEVELOPMENT_PROFILE
    extern int acp_render_detail_install(so_module *);
    if(acp_render_detail_install(m))return -1;
#endif
    return 0;
}
