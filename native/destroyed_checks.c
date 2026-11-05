/* Since 00.56 every live entity carries an explicit IsDestroyed=false
 * (support/vita_entity_state.lua). The engine's native collision and trigger
 * reports test lua_getfield(entity,"IsDestroyed") with lua_type(L,-1)!=nil,
 * so they dropped every report for a live entity: no Box2D contact reached
 * OnCollision2D (ships sailed through islands, ships, mines and barrels and
 * fleets never spotted the player), nor did LuaMotion collisions and
 * triggers. Each of those tests now calls lua_toboolean (same arguments,
 * result compared with 0 as before): only a true IsDestroyed skips. */
#include "acp_native.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <string.h>

#define LUA_TOBOOLEAN 0x6bb388u   /* Thumb */

int acp_destroyed_checks_install(so_module *m){
    static const struct {uint32_t site;uint8_t guard[4];} sites[]={   /* bl lua_type */
        {0x53eb48,{0x7c,0xf1,0xb4,0xf9}},   /* Box2DContactListener::CallOnCollision2D */
        {0x59ca54,{0x1e,0xf1,0x2e,0xfa}},   /* LuaMotion::PushCollisions, first body */
        {0x59cad0,{0x1e,0xf1,0xf0,0xf9}},   /* LuaMotion::PushCollisions, second body */
        {0x59ce40,{0x1e,0xf1,0x38,0xf8}},   /* LuaMotion::PushTriggers */
        {0x5a8364,{0x12,0xf1,0xa6,0xfd}},   /* LuaNewton::PushATrigger, trigger */
        {0x5a83d6,{0x12,0xf1,0x6d,0xfd}},   /* LuaNewton::PushATrigger, other */
    };
    if((so_symbol(m,"lua_toboolean")&~1u)!=m->load_addr+LUA_TOBOOLEAN)return -1;
    for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++)
        if(memcmp((void *)(m->load_addr+sites[i].site),sites[i].guard,4))return -1;
#ifdef ACP_DEVELOPMENT_PROFILE
    SceIoStat st;   /* api/no-collision-fix: the 00.56-00.79 behaviour, for comparisons */
    if(sceIoGetstat(ACP_DATA_PATH "/api/no-collision-fix",&st)>=0){acp_log("Collision reports: off (profile comparison)");return 0;}
#endif
    for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++){
        uintptr_t site=m->load_addr+sites[i].site;
        int32_t delta=(int32_t)(m->load_addr+LUA_TOBOOLEAN-site-4);
        if(delta<-(1<<24)||delta>=(1<<24)||(delta&1))return -1;
        uint32_t bits=(uint32_t)delta,s=(bits>>24)&1;
        uint16_t call[2]={(uint16_t)(0xf000|(s<<10)|((bits>>12)&1023)),
            (uint16_t)(0xd000|((1^((bits>>23)&1)^s)<<13)|((1^((bits>>22)&1)^s)<<11)|((bits>>1)&2047))};
        if(kuKernelCpuUnrestrictedMemcpy((void *)site,call,4)<0)return -1;
        kuKernelFlushCaches((void *)site,4);
    }
    return 0;
}
