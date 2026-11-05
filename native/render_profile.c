#ifdef ACP_DEVELOPMENT_PROFILE
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
static uintptr_t (*originals[26])(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
#ifdef ACP_DEVELOPMENT_PROFILE
static int ids[26];
#endif
extern volatile unsigned acp_presented_frames;
static struct {unsigned count,total,maximum;} timing[26];
extern volatile unsigned acp_draw_calls;
static struct {uintptr_t camera,scene;unsigned culls,cull_us,groups,group_us,draws,meshes,visible,dirty,hidden_dirty;} cameras[32];
static unsigned inspect[4];
static void inspect_scene(uintptr_t scene){
    memset(inspect,0,sizeof(inspect));
    uintptr_t sentinel=scene+0x2c,node=*(uintptr_t *)(scene+0x34);
    while(node!=sentinel && inspect[0]<16384){
        unsigned char *mesh=*(unsigned char **)(node+0x10);
        ++inspect[0];inspect[1]+=mesh[0x64]!=0;inspect[2]+=mesh[0xd9]!=0;
        inspect[3]+=!mesh[0x64]&&mesh[0xd9];
        if(*(uintptr_t *)(node+12)){
            node=*(uintptr_t *)(node+12);
            while(*(uintptr_t *)(node+8))node=*(uintptr_t *)(node+8);
        }else{
            uintptr_t parent=*(uintptr_t *)(node+4);
            while(node==*(uintptr_t *)(parent+12)){node=parent;parent=*(uintptr_t *)(parent+4);}
            if(*(uintptr_t *)(node+12)!=parent)node=parent;
        }
    }
}
static uintptr_t invoke(unsigned i,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){
#ifdef ACP_DEVELOPMENT_PROFILE
    int active=acp_method_phase(ids[i],1);
#endif
    int sample=!(acp_presented_frames&63u);
    if(sample && i==20)inspect_scene(a);
    unsigned begin=sample?sceKernelGetProcessTimeLow():0;
    unsigned draws=acp_draw_calls;
    uintptr_t result=originals[i](a,b,c,d);
    if(sample){
        unsigned elapsed=sceKernelGetProcessTimeLow()-begin;
        timing[i].count++;timing[i].total+=elapsed;
        if(elapsed>timing[i].maximum)timing[i].maximum=elapsed;
        if(i==20 || i==21)for(unsigned k=0;k<32;k++){
            if(cameras[k].camera && cameras[k].camera!=b)continue;
            cameras[k].camera=b;
            if(i==20){cameras[k].scene=a;cameras[k].culls++;cameras[k].cull_us+=elapsed;
                cameras[k].meshes+=inspect[0];cameras[k].visible+=inspect[1];cameras[k].dirty+=inspect[2];cameras[k].hidden_dirty+=inspect[3];}
            else{cameras[k].groups++;cameras[k].group_us+=elapsed;cameras[k].draws+=acp_draw_calls-draws;}
            break;
        }
    }
#ifdef ACP_DEVELOPMENT_PROFILE
    if(active)acp_method_phase(ids[i],0);
#endif
    return result;
}
#define WRAPPER(n) static uintptr_t wrap##n(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){return invoke(n,a,b,c,d);}
WRAPPER(0) WRAPPER(1) WRAPPER(2) WRAPPER(3) WRAPPER(4) WRAPPER(5) WRAPPER(6) WRAPPER(7) WRAPPER(8) WRAPPER(9) WRAPPER(10) WRAPPER(11) WRAPPER(12) WRAPPER(13) WRAPPER(14) WRAPPER(15) WRAPPER(16) WRAPPER(17) WRAPPER(18) WRAPPER(19) WRAPPER(20) WRAPPER(21) WRAPPER(22) WRAPPER(23) WRAPPER(24) WRAPPER(25)
void acp_render_detail_report(void){
    for(unsigned k=0;k<32;k++)if(cameras[k].camera){
        acp_log("Camera detail (1/64): camera=%p scene=%p culls=%u cull-us=%u groups=%u group-us=%u draws=%u",
            (void *)cameras[k].camera,(void *)cameras[k].scene,cameras[k].culls,cameras[k].cull_us,
            cameras[k].groups,cameras[k].group_us,cameras[k].draws);
        acp_log("Camera meshes: camera=%p meshes=%u visible=%u dirty=%u hidden-dirty=%u",(void *)cameras[k].camera,cameras[k].meshes,cameras[k].visible,cameras[k].dirty,cameras[k].hidden_dirty);
    }
    memset(cameras,0,sizeof(cameras));
    static const char *names[]={"view-projection","world-matrix","shader-apply","view-projection","shader-apply","before-draw","after-draw","world-matrix","world-matrix","view-projection","view-projection","shader-apply","shader-apply","before-draw","view-projection","world-matrix","after-draw","world-matrix","view-projection","view-projection","culling","render-group","global-params","value-apply","material-params","object-params"};
    for(unsigned i=0;i<26;i++){
        if(timing[i].count)acp_log("Render detail (1/64): %s total=%u us n=%u max=%u us",
            names[i],timing[i].total,timing[i].count,timing[i].maximum);
        timing[i].count=timing[i].total=timing[i].maximum=0;
    }
}
int acp_render_detail_install(so_module *m){
    static const struct {uint32_t site,target;uint8_t guard[4];const char *name;uintptr_t wrapper;int link;} hooks[]={
        {0x794128,0x788039,{0xf3,0xf7,0x86,0xff},"render:view-projection",(uintptr_t)wrap0,1},
        {0x794130,0x787f85,{0xf3,0xf7,0x28,0xff},"render:world-matrix",(uintptr_t)wrap1,1},
        {0x794294,0x79e261,{0x09,0xf0,0xe4,0xff},"render:shader-apply",(uintptr_t)wrap2,1},
        {0x794334,0x788039,{0xf3,0xf7,0x80,0xfe},"render:view-projection",(uintptr_t)wrap3,1},
        {0x79434e,0x79e261,{0x09,0xf0,0x87,0xff},"render:shader-apply",(uintptr_t)wrap4,1},
        {0x794354,0x792529,{0xfe,0xf7,0xe8,0xf8},"render:before-draw",(uintptr_t)wrap5,1},
        {0x7943ba,0x792b91,{0xfe,0xf7,0xe9,0xfb},"render:after-draw",(uintptr_t)wrap6,1},
        {0x7943d0,0x787f85,{0xf3,0xf7,0xd8,0xfd},"render:world-matrix",(uintptr_t)wrap7,1},
        {0x7943da,0x787f85,{0xf3,0xf7,0xd3,0xfd},"render:world-matrix",(uintptr_t)wrap8,1},
        {0x7943f8,0x788039,{0xf3,0xf7,0x1e,0xfe},"render:view-projection",(uintptr_t)wrap9,1},
        {0x79448a,0x788039,{0xf3,0xf7,0xd5,0xfd},"render:view-projection",(uintptr_t)wrap10,1},
        {0x7946a6,0x79e261,{0x09,0xf0,0xdb,0xfd},"render:shader-apply",(uintptr_t)wrap11,1},
        {0x7946c6,0x79e261,{0x09,0xf0,0xcb,0xfd},"render:shader-apply",(uintptr_t)wrap12,1},
        {0x7946cc,0x792529,{0xfd,0xf7,0x2c,0xff},"render:before-draw",(uintptr_t)wrap13,1},
        {0x794706,0x788039,{0xf3,0xf7,0x97,0xfc},"render:view-projection",(uintptr_t)wrap14,1},
        {0x794726,0x787f85,{0xf3,0xf7,0x2d,0xfc},"render:world-matrix",(uintptr_t)wrap15,1},
        {0x79473a,0x792b91,{0xfe,0xf7,0x29,0xfa},"render:after-draw",(uintptr_t)wrap16,1},
        {0x79476e,0x787f85,{0xf3,0xf7,0x09,0xfc},"render:world-matrix",(uintptr_t)wrap17,1},
        {0x79478c,0x788039,{0xf3,0xf7,0x54,0xfc},"render:view-projection",(uintptr_t)wrap18,1},
        {0x794820,0x788039,{0xf3,0xf7,0x0a,0xfc},"render:view-projection",(uintptr_t)wrap19,1},
        {0x797130,0x795e69,{0xfe,0xf7,0x9a,0xfe},"render:culling",(uintptr_t)wrap20,1},
        {0x797152,0x793f55,{0xfc,0xf7,0xff,0xfe},"render:render-group",(uintptr_t)wrap21,1},
        {0x79e5ce,0x799c29,{0xfb,0xf7,0x2b,0xfb},"render:global-params",(uintptr_t)wrap22,1},
        {0x79e5f0,0x79da65,{0xff,0xf7,0x38,0xfa},"render:value-apply",(uintptr_t)wrap23,1},
        {0x79e612,0x799c75,{0xfb,0xf7,0x2f,0xfb},"render:material-params",(uintptr_t)wrap24,1},
        {0x79e61e,0x799ca1,{0xfb,0xf7,0x3f,0xfb},"render:object-params",(uintptr_t)wrap25,1},
    };
    for(unsigned i=0;i<26;i++)if(memcmp((void *)(m->load_addr+hooks[i].site),hooks[i].guard,4))return -1;
    for(unsigned i=0;i<26;i++){
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
    return 0;
}

#endif
