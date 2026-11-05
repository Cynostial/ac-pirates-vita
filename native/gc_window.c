/* Decide before waking the collector, while Lua still owns its state. The
 * original start/stop pair, worker mutex and write barriers stay untouched. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include "acp_gc.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
static int (*original_required)(void *);
static unsigned windows,skipped;
extern void acp_gc_window_seen(void *);
int acp_gc_window_required(void *state){
    int enabled=original_required(state);
    if(!enabled)return 0;
    unsigned char *g=*(unsigned char **)((unsigned char *)state+8);
    /* This is a verified safe collection opportunity, even when no cycle is
     * due. Keep bounded foreground deferral alive between actual cycles. */
    acp_gc_window_seen(g);
    __atomic_fetch_add(&windows,1,__ATOMIC_RELAXED);
    if(!acp_gc_cycle_due(g[0x1d],*(uint32_t *)(g+0x14),
                        *(uint32_t *)(g+0x44),*(uint32_t *)(g+0x48))){
        __atomic_fetch_add(&skipped,1,__ATOMIC_RELAXED);return 0;
    }
    return enabled;
}
void acp_gc_window_report(void){
    acp_log("GC windows: eligible=%u idle-skipped=%u",
        __atomic_exchange_n(&windows,0,__ATOMIC_RELAXED),
        __atomic_exchange_n(&skipped,0,__ATOMIC_RELAXED));
}
int acp_gc_window_install(so_module *m){
    const uint32_t sites[]={0x528fc4,0x57a884,0x57a93c,0x612ce2};
    const uint8_t guards[][4]={{0x48,0xf1,0x88,0xf8},{0xf6,0xf0,0x28,0xfc},
                             {0xf6,0xf0,0xcc,0xfb},{0x5e,0xf0,0xf9,0xf9}};
    for(unsigned i=0;i<4;i++)if(memcmp((void *)(m->load_addr+sites[i]),guards[i],4))return -1;
    original_required=(void *)so_symbol(m,"_ZN13LuaBindTools232IsAsyncGarbageCollectionRequiredEP9lua_State");
    if(!original_required)return -1;
    for(unsigned i=0;i<4;i++){
        uintptr_t site=m->load_addr+sites[i],stub=(m->patch_head+3u)&~3u;
        if(stub<m->patch_base||stub+8>m->patch_base+m->patch_size)return -1;
        int32_t delta=(int32_t)(stub-site-4);
        if(delta<-(1<<24)||delta>=(1<<24)||(delta&1))return -1;
        uint32_t bits=(uint32_t)delta,s=(bits>>24)&1;
        uint16_t branch[2]={(uint16_t)(0xf000|(s<<10)|((bits>>12)&1023)),
            (uint16_t)(0xd000|((1^((bits>>23)&1)^s)<<13)|((1^((bits>>22)&1)^s)<<11)|((bits>>1)&2047))};
        uint8_t jump[8];if(acp_game_patch_build((uint32_t)(uintptr_t)acp_gc_window_required,jump))return -1;
        if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,8)<0)return -1;
        kuKernelFlushCaches((void *)stub,8);
        if(kuKernelCpuUnrestrictedMemcpy((void *)site,branch,4)<0)return -1;
        m->patch_head=stub+8;
    }
    acp_log("Lua GC: skip idle worker windows before signalling; original collector synchronization retained");
    return 0;
}
