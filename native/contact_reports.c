/* Box2D contact reports, one per body pair and physics step.
 * LuaBox2D's listener records every manifold point of every touching contact
 * twice (PreSolve, once per body) and ProcessFrameContacts calls the
 * receiver's Lua OnCollision2D for each record: a push of both entities, two
 * new Vector2 userdata and a Lua call. Near land the player's island
 * proximity circle touches dozens of island triangles at once, all the same
 * Island2DCollisionShape entity, so a step made hundreds of identical calls.
 * Every OnCollision2D in the game stores the hit by the other entity
 * (PakCollision2DNotifier keeps the last one; the others only test the other
 * entity's tags), so one call per pair, in the order the pairs first occur,
 * with the pair's last hit point and normal, leaves the same state. With
 * collision callbacks off (cached reports), the original runs unchanged. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <string.h>

#define SITE 0x53a6e8u                 /* Physics2DManager OnUpdate: bl ProcessFrameContacts */
#define PROCESS_FRAME_CONTACTS 0x53ecd4u
#define CALL_ON_COLLISION 0x53eb20u    /* Thumb */
#define MAX_PAIRS 256

typedef struct {float x,y;} vec2;
typedef struct {vec2 point,normal;void *body,*other;} contact;   /* the listener's ContactInfo */
typedef void (*report_fn)(void *,void *,void *,vec2,vec2);
typedef void (*process_fn)(void *);
static report_fn report;
static process_fn original;
static struct {unsigned steps,records,calls;} stats;
static int merge=1;   /* profile builds: api/no-contact-merge turns it off for comparisons */

void acp_contact_reports(uint8_t *listener){
    /* Listener: frame records [16,20) (std::vector), callbacks enabled at 28. */
    contact *begin=*(contact **)(listener+16),*end=*(contact **)(listener+20);
    unsigned n=(unsigned)(end-begin);
    if(!merge||!listener[28]||n<2){original(listener);stats.steps++;stats.records+=n;stats.calls+=n;return;}
    static unsigned first[MAX_PAIRS],last[MAX_PAIRS];
    unsigned pairs=0,i=0;
    for(;i<n;i++){
        unsigned k=0;
        while(k<pairs&&(begin[first[k]].body!=begin[i].body||begin[first[k]].other!=begin[i].other))k++;
        if(k<pairs){last[k]=i;continue;}
        if(pairs==MAX_PAIRS)break;     /* not expected: the rest is reported as recorded */
        first[pairs]=last[pairs]=i;pairs++;
    }
    for(unsigned k=0;k<pairs;k++){
        const contact *c=begin+last[k];
        report(listener,begin[first[k]].body,begin[first[k]].other,c->point,c->normal);
    }
    for(unsigned j=i;j<n;j++)report(listener,begin[j].body,begin[j].other,begin[j].point,begin[j].normal);
    *(contact **)(listener+20)=begin;
    stats.steps++;stats.records+=n;stats.calls+=pairs+(n-i);
}
void acp_contact_reports_log(void){
    if(stats.steps)acp_log("Box2D reports: %u steps, %u records, %u calls",stats.steps,stats.records,stats.calls);
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
int acp_contact_reports_install(so_module *m){
    static const uint8_t guard[4]={0x04,0xf0,0xf4,0xfa};   /* bl ProcessFrameContacts */
    uintptr_t site=m->load_addr+SITE,stub=(m->patch_head+3u)&~3u;
    if(memcmp((void *)site,guard,4)||stub<m->patch_base||stub+8>m->patch_base+m->patch_size)return -1;
#ifdef ACP_DEVELOPMENT_PROFILE
    SceIoStat st;
    if(sceIoGetstat(ACP_DATA_PATH "/api/no-contact-merge",&st)>=0){merge=0;acp_log("Box2D reports: merging off (profile comparison)");}
#endif
    report=(report_fn)(m->load_addr+CALL_ON_COLLISION+1);
    original=(process_fn)(m->load_addr+PROCESS_FRAME_CONTACTS+1);
    uint8_t jump[8];uint16_t call[2];
    if(acp_game_patch_build((uint32_t)(uintptr_t)acp_contact_reports,jump)||branch(site,stub,call))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,8)<0)return -1;
    kuKernelFlushCaches((void *)stub,8);
    if(kuKernelCpuUnrestrictedMemcpy((void *)site,call,4)<0)return -1;
    kuKernelFlushCaches((void *)site,4);
    m->patch_head=stub+8;
    return 0;
}
