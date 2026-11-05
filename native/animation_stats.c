/* EdgeAnimationManager cost on hardware: the number of animated bodies the
 * engine updates per frame and the time AnimManager::UpdateAnimatedBodies
 * takes, logged with the frame timing. Hardware sailing logs show the
 * manager at about 1 ms or about 5 ms a frame in the same view; these
 * counts tell the two states apart. Behaviour is unchanged. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <string.h>

#define SITE 0x54eeacu                    /* AnimManager::Interface::OnUpdate: bl UpdateAnimatedBodies */
#define UPDATE_ANIMATED_BODIES 0x54ee3cu  /* Thumb */

typedef void (*update_fn)(void *,float);
static update_fn original;
static struct {unsigned frames,bodies,active,most,heavy;float slowest;uint64_t us,longest;} stats;

void acp_animation_update(uint8_t *manager,float dt){
    /* Registered bodies: std::vector [4,8). A body with HasToBeUpdated off
     * (byte 112) is skipped; each body keeps its last update time in ms (104). */
    uint8_t **begin=*(uint8_t ***)(manager+4),**end=*(uint8_t ***)(manager+8);
    uint64_t start=sceKernelGetProcessTimeWide();
    original(manager,dt);
    uint64_t us=sceKernelGetProcessTimeWide()-start;
    unsigned active=0;
    for(uint8_t **b=begin;b<end;b++){
        if(!(*b)[112])continue;
        float ms;memcpy(&ms,*b+104,sizeof(ms));
        active++;
        if(ms>=0.1f)stats.heavy++;
        if(ms>stats.slowest)stats.slowest=ms;
    }
    stats.frames++;stats.bodies+=(unsigned)(end-begin);stats.active+=active;stats.us+=us;
    if(active>stats.most)stats.most=active;
    if(us>stats.longest)stats.longest=us;
}
void acp_animation_stats_log(void){
    if(stats.frames)acp_log("Animation: %u frames, bodies=%u active mean=%u max=%u, >=0.1 ms per frame=%u, slowest=%u us, update mean=%llu us max=%llu us",
        stats.frames,stats.bodies/stats.frames,stats.active/stats.frames,stats.most,stats.heavy/stats.frames,
        (unsigned)(stats.slowest*1000.0f),stats.us/stats.frames,stats.longest);
    memset(&stats,0,sizeof(stats));
}

static int branch(uintptr_t site,uintptr_t stub,uint16_t out[2]){
    int32_t delta=(int32_t)(stub-site-4);
    if(delta<-(1<<24)||delta>=(1<<24)||(delta&1))return -1;
    uint32_t bits=(uint32_t)delta,s=(bits>>24)&1;
    out[0]=(uint16_t)(0xf000|(s<<10)|((bits>>12)&1023));
    out[1]=(uint16_t)(0xd000|((1^((bits>>23)&1)^s)<<13)|((1^((bits>>22)&1)^s)<<11)|((bits>>1)&2047));
    return 0;
}
int acp_animation_stats_install(so_module *m){
    static const uint8_t guard[4]={0xff,0xf7,0xc6,0xff};   /* bl UpdateAnimatedBodies */
    uintptr_t site=m->load_addr+SITE,stub=(m->patch_head+3u)&~3u;
    if(memcmp((void *)site,guard,4)||stub<m->patch_base||stub+8>m->patch_base+m->patch_size)return -1;
    original=(update_fn)(m->load_addr+UPDATE_ANIMATED_BODIES+1);
    uint8_t jump[8];uint16_t call[2];
    if(acp_game_patch_build((uint32_t)(uintptr_t)acp_animation_update,jump)||branch(site,stub,call))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,8)<0)return -1;
    kuKernelFlushCaches((void *)stub,8);
    if(kuKernelCpuUnrestrictedMemcpy((void *)site,call,4)<0)return -1;
    kuKernelFlushCaches((void *)site,4);
    m->patch_head=stub+8;
    return 0;
}
