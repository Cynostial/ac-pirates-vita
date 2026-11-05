/* Cache the final translated program and its matrix/binding metadata.
 * Keying pre-translation GLSL alone is unsafe: linked varyings change semantics.
 * Keep upstream HAVE_SHADER_CACHE disabled (it uses that weaker key). */
#include <stdlib.h>
#include <psp2/kernel/threadmgr.h>
#include "acp_shader_cache.h"
extern void acp_log(const char *,...);
#define ACP_GXP_DIRECTORY "ux0:data/acpirates/shaders-v1"
static unsigned acp_gxp_hits,acp_gxp_misses;
static void acp_gxp_key(shader *s,GLboolean bindings,char key[65]){
    struct {uint32_t stage,options,fastmath,fastprecision,fastint,bindings;binds_map map;} cfg;
    memset(&cfg,0,sizeof(cfg));
    cfg.stage=s->type;cfg.options=compiler_opts;
    cfg.fastmath=strstr(s->source,"u_WaterColorAttenuation")?0:compiler_fastmath;
    cfg.fastprecision=compiler_fastprecision;cfg.fastint=compiler_fastint;cfg.bindings=bindings;
    if(bindings){
        for(unsigned i=0;i<MAX_CG_TEXCOORD_ID;i++)if(s->semantics.texcoord_used[i]){
            cfg.map.texcoord_used[i]=1;strncpy(cfg.map.texcoord_names[i],s->semantics.texcoord_names[i],63);
        }
        for(unsigned i=0;i<MAX_CG_COLOR_ID;i++)if(s->semantics.color_used[i]){
            cfg.map.color_used[i]=1;strncpy(cfg.map.color_names[i],s->semantics.color_names[i],63);
        }
    }
    acp_shader_cache_key(s->source,strlen(s->source),&cfg,sizeof(cfg),key);
}
static int acp_gxp_load(shader *s,GLboolean bindings,const char *key){
    void *blob=NULL;size_t n=acp_shader_cache_read(ACP_GXP_DIRECTORY,key,&blob);
    if(!n)return 0;
    uint32_t count=0;int ok=0;matrix_uniform *mat=NULL;SceGxmProgram *prog=NULL;
    if(n<4)goto done;
    memcpy(&count,blob,4);
    size_t offset=4u+(size_t)count*4u+(bindings?sizeof(binds_map):0);
    if(count>256||offset>n||n-offset<64)goto done;
    unsigned char *gxp=(unsigned char *)blob+offset;
    if(memcmp(gxp,"GXP\0",4))goto done;
    prog=vglMalloc(n-offset);if(!prog)goto done;
    memcpy(prog,gxp,n-offset);
    /* ShaRK rounds its output allocation to four bytes; the embedded GXP
     * length excludes that trailing padding. Accept only that exact padding. */
    size_t gxp_size=sceGxmProgramGetSize(prog);
    if(gxp_size<64||((gxp_size+3u)&~(size_t)3u)!=n-offset||sceGxmProgramCheck(prog)<0)goto done;
    if(sceGxmProgramGetType(prog)!=(s->type==GL_FRAGMENT_SHADER?SCE_GXM_FRAGMENT_PROGRAM:SCE_GXM_VERTEX_PROGRAM))goto done;
    uint32_t parameters=sceGxmProgramGetParameterCount(prog);
    uintptr_t param_offset=(uintptr_t)vglProgramGetParameterBase(prog)-(uintptr_t)prog;
    if(parameters>4096||param_offset>gxp_size||parameters>(gxp_size-param_offset)/16)goto done;
    for(unsigned i=0;i<count;i++){
        uint32_t index;memcpy(&index,(unsigned char *)blob+4+i*4,4);
        if(index>=parameters)goto done;
        matrix_uniform *m=vglMalloc(sizeof(*m));if(!m)goto done;
        m->ptr=(SceGxmProgramParameter *)((unsigned char *)prog+param_offset+index*16);
        m->chain=mat;mat=m;
    }
    if(sceGxmShaderPatcherRegisterProgram(gxm_shader_patcher,prog,&s->id)<0)goto done;
    if(bindings)memcpy(&s->semantics,(unsigned char *)blob+4+count*4,sizeof(binds_map));
    s->prog=prog;s->size=n-offset;s->mat=mat;
    s->unif_buf_size=sceGxmProgramGetDefaultUniformBufferSize(prog);
    vgl_free(s->source);s->source=NULL;
    ok=1;++acp_gxp_hits;
    if(acp_gxp_hits<=4||!(acp_gxp_hits%32))acp_log("GXP cache: hits=%u misses=%u",acp_gxp_hits,acp_gxp_misses);
done:
    if(!ok){while(mat){matrix_uniform *next=mat->chain;vgl_free(mat);mat=next;}if(prog)vgl_free(prog);}
    free(blob);return ok;
}
static void acp_gxp_save(shader *s,GLboolean bindings,const char *key,uint64_t start){
    unsigned us=(unsigned)(sceKernelGetProcessTimeWide()-start);++acp_gxp_misses;
    int saved=-1;
    if(s->prog&&!s->unif_blk){
        /* ShaccCg also enumerates optimized-out matrices. Their GXP parameter
         * is NULL and upstream serialize_shader emits a bogus index for them.
         * Store only matrices which actually exist in the compiled program. */
        uint32_t indices[256],count=0,parameters=sceGxmProgramGetParameterCount(s->prog);
        int overflow=0;
        for(matrix_uniform *m=s->mat;m;m=m->chain)if(m->ptr){
            uint32_t index=sceGxmProgramParameterGetIndex(s->prog,m->ptr);
            if(index<parameters){if(count==256){overflow=1;break;}indices[count++]=index;}
        }
        size_t n=4u+count*4u+(bindings?sizeof(binds_map):0)+s->size;
        if(!overflow&&n<=ACP_SHADER_CACHE_MAX){
            void *blob=malloc(n);
            if(blob){
                unsigned char *out=blob;memcpy(out,&count,4);out+=4;
                memcpy(out,indices,count*4u);out+=count*4u;
                if(bindings){memcpy(out,&s->semantics,sizeof(binds_map));out+=sizeof(binds_map);}
                memcpy(out,s->prog,s->size);sceIoMkdir(ACP_GXP_DIRECTORY,0777);
                saved=acp_shader_cache_write(ACP_GXP_DIRECTORY,key,blob,n);free(blob);}
        }
    }
    acp_log("GXP compile: %u us stage=%u saved=%d hits=%u misses=%u",us,s->type,saved==0,acp_gxp_hits,acp_gxp_misses);
}
