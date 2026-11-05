/* Private VM-call-site sampler. No hooks are present in a release package. */
#ifdef ACP_DEVELOPMENT_PROFILE
#include "acp_native.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <string.h>
extern volatile unsigned acp_presented_frames;
volatile unsigned acp_callback_profile_request;
extern int acp_class_index(void *);
typedef struct {uintptr_t fn,key;unsigned calls,self,total;} Entry;
static Entry entries[2048];
static struct {unsigned child;} stack[128];
static unsigned depth,active,generation,samples,seen;
static uintptr_t image_base;
static unsigned now(void){return sceKernelGetProcessTimeLow();}
static int callback(void *state,int (*fn)(void *)){
    if(!active||depth>=128)return fn(state);
    unsigned level=depth++,epoch=generation;
    uintptr_t key=0;
    if(fn==acp_class_index){
        const uint32_t *base=*(const uint32_t **)((char *)state+16);
        if(base[3]==~4u)key=base[2];
    }
    stack[level].child=0;
    unsigned begin=now();
    int result=fn(state);
    unsigned elapsed=now()-begin;
    /* A Lua error can unwind several callbacks without returning. */
    if(epoch!=generation||depth!=level+1)return result;
    depth=level;
    unsigned child=stack[level].child;
    if(level)stack[level-1].child+=elapsed;
    unsigned h=(((uintptr_t)fn>>2)^(key>>3))&2047u;
    for(unsigned n=0;n<2048;n++,h=(h+1)&2047u){
        Entry *e=entries+h;
        if(!e->fn||(e->fn==(uintptr_t)fn&&e->key==key)){
            e->fn=(uintptr_t)fn;e->key=key;e->calls++;e->total+=elapsed;
            e->self+=elapsed>=child?elapsed-child:0;break;
        }
    }
    return result;
}
void acp_callback_profile_frame(int begin){
    if(begin){
        unsigned request=__atomic_load_n(&acp_callback_profile_request,__ATOMIC_ACQUIRE);
        if(request!=seen){seen=request;samples=0;}
        generation++;depth=0;active=seen&&samples<12&&!(acp_presented_frames&127u);
        if(active)memset(entries,0,sizeof(entries));
    }else if(active){
        active=0;generation++;depth=0;samples++;
        unsigned calls=0,total=0;
        for(unsigned i=0;i<2048;i++){calls+=entries[i].calls;total+=entries[i].self;}
        acp_log("Native callbacks sample=%u frame=%u calls=%u self-total=%u us (instrumented)",samples,acp_presented_frames,calls,total);
        for(unsigned n=0;n<40;n++){
            unsigned best=0;
            for(unsigned i=1;i<2048;i++)if(entries[i].self>entries[best].self)best=i;
            Entry e=entries[best];if(!e.self)break;
            acp_log("Native callback: fn=%08x offset=%08x self=%u us inclusive=%u us calls=%u key=%.60s",(unsigned)e.fn,(unsigned)(e.fn-image_base),e.self,e.total,e.calls,e.key?(const char *)(e.key+16):"");
            entries[best].self=0;
        }
    }
}
int acp_callback_profile_install(so_module *m){
    uintptr_t site=m->load_addr+0x6cfb08,stub=(m->patch_head+3u)&~3u;
    if(*(uint32_t *)site!=0xe12fff33u||stub<m->patch_base||stub+12>m->patch_base+m->patch_size)return -1;
    int32_t displacement=(int32_t)(stub-site-8);
    if(displacement<-(1<<25)||displacement>=(1<<25)||(displacement&3))return -1;
    uint32_t jump[]={0xe1a01003,0xe51ff004,(uint32_t)(uintptr_t)callback};
    uint32_t branch=0xeb000000u|(((uint32_t)displacement>>2)&0xffffffu);
    if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,sizeof(jump))<0)return -1;
    kuKernelFlushCaches((void *)stub,sizeof(jump));
    if(kuKernelCpuUnrestrictedMemcpy((void *)site,&branch,4)<0)return -1;
    m->patch_head=stub+sizeof(jump);image_base=m->load_addr;
    acp_log("Private native callback sampler installed; waiting for local API request");return 0;
}
#endif
