/* Correct the verified animation-node removal's Lua API stack indices. */
#include "acp_native.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
int acp_animation_nodes_install(so_module *m){
    /* AnimBranch::lua_removeSubnode pushes a value/nil before lua_rawseti.
     * The subnode table is then at -2, not -1. In the original unchecked VM
     * the incorrect nil slot could expose a stale pointer as a table. */
    const uint32_t sites[]={0x54e0da,0x54e0f2};
    const uint8_t old_index[]={0x4f,0xf0,0xff,0x31};
    const uint8_t new_index[]={0x6f,0xf0,0x01,0x01};
    for(unsigned i=0;i<2;i++)if(memcmp((void *)(m->load_addr+sites[i]),old_index,4))return -1;
    for(unsigned i=0;i<2;i++)if(kuKernelCpuUnrestrictedMemcpy((void *)(m->load_addr+sites[i]),new_index,4)<0)return -1;
    acp_log("Animation nodes: corrected table index for removal and compaction");return 0;
}
