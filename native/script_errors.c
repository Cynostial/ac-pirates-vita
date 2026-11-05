/* Preserve the error object before Spark's CallLuaMethod removes it. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
static int (*original_pcall)(void *,int,int,int);
static int (*type)(void *,int);
static const char *(*string)(void *,int,size_t *);
static int checked_pcall(void *state,int nargs,int results,int handler){
    int result=original_pcall(state,nargs,results,handler);
    if(result){
        int kind=type(state,-1);
        const char *message=kind==4?string(state,-1,NULL):NULL;
        acp_log("Engine Lua call failed: status=%d type=%d error=%s",result,kind,message?message:"[non-string error object]");
    }
    return result;
}
int acp_script_errors_install(so_module *m){
    const uint32_t sites[]={0x528a00,0x528a42};
    const uint8_t guards[2][4]={{0x93,0xf1,0x5e,0xfb},{0x93,0xf1,0x3d,0xfb}};
    for(unsigned i=0;i<2;i++)if(memcmp((void *)(m->load_addr+sites[i]),guards[i],4))return -1;
    original_pcall=(void *)so_symbol(m,"lua_pcall");
    type=(void *)so_symbol(m,"lua_type");string=(void *)so_symbol(m,"lua_tolstring");
    if(!original_pcall||!type||!string)return -1;
    for(unsigned i=0;i<2;i++){
        uintptr_t site=m->load_addr+sites[i],stub=(m->patch_head+3u)&~3u;
        if(stub<m->patch_base||stub+8>m->patch_base+m->patch_size)return -1;
        int32_t delta=(int32_t)(stub-site-4);
        if(delta<-(1<<24)||delta>=(1<<24)||(delta&1))return -1;
        uint32_t bits=(uint32_t)delta,s=(bits>>24)&1;
        uint16_t branch[2]={(uint16_t)(0xf000|(s<<10)|((bits>>12)&1023)),
            (uint16_t)(0xd000|((1^((bits>>23)&1)^s)<<13)|((1^((bits>>22)&1)^s)<<11)|((bits>>1)&2047))};
        uint8_t jump[8];if(acp_game_patch_build((uint32_t)(uintptr_t)checked_pcall,jump))return -1;
        if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,8)<0)return -1;
        kuKernelFlushCaches((void *)stub,8);
        if(kuKernelCpuUnrestrictedMemcpy((void *)site,branch,4)<0)return -1;
        m->patch_head=stub+8;
    }
    return 0;
}
