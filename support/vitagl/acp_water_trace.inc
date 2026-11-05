/* Cache both ocean and non-ocean programs. This is queried for every draw,
 * so shader reflection must happen once per link, not once per draw. */
typedef struct { unsigned ready; float *filter; } acp_water_program;
static acp_water_program acp_water_programs[MAX_CUSTOM_PROGRAMS];
void acp_vitagl_water_forget(unsigned id) {
    if(id && id<=MAX_CUSTOM_PROGRAMS)
        memset(&acp_water_programs[id-1],0,sizeof(acp_water_programs[0]));
}
static float *acp_water_filter(unsigned id) {
    if(!id || id>MAX_CUSTOM_PROGRAMS)return NULL;
    acp_water_program *cached=&acp_water_programs[id-1];
    if(!cached->ready){
        program *p=&progs[id-1];
        if(p->status!=PROG_LINKED)return NULL;
        cached->ready=1;
        for(unsigned i=0;i<p->frag_uniforms_num;++i){
            uniform *u=&p->frag_uniforms[i];
            if(u->type==UNIFORM_DATA && u->fptr &&
               !strcmp(sceGxmProgramParameterGetName(u->ptr),"u_ACPReflectionFilter")){
                cached->filter=(float *)u->fptr;break;
            }
        }
    }
    return cached->filter;
}

/* The mailbox is enabled only by a developer flag. Save and restore around
 * each draw so comparisons cannot permanently alter the game's material. */
void acp_vitagl_water_compare(int before) {
    extern volatile unsigned acp_water_debug_mode;
    static float *target;
    static float saved[3];
    static unsigned components;
    static texture *normal_texture;
    static SceGxmTexture normal_saved;
    if (!before) {
        if(normal_texture){normal_texture->gxm_tex=normal_saved;normal_texture=NULL;}
        if (target) { memcpy(target,saved,components*sizeof(float)); dirty_shader_frag_unifs=GL_TRUE; target=NULL; }
        return;
    }
    unsigned mode=__atomic_load_n(&acp_water_debug_mode,__ATOMIC_ACQUIRE);
    float *filter=acp_water_filter(cur_program);
    if(!filter)return;
    program *p=&progs[cur_program-1];
    float radius=mode==6?0.0f:(mode==9?1.0f:2.0f);
    if(*filter!=radius){*filter=radius;dirty_shader_frag_unifs=GL_TRUE;}
    if(mode==7){
        for(unsigned i=0;i<p->frag_uniforms_num;++i){
            uniform *u=&p->frag_uniforms[i];
            if(u->type!=UNIFORM_DATA && !strcmp(sceGxmProgramParameterGetName(u->ptr),"u_NormalMap")){
                normal_texture=&texture_slots[texture_units[u->sampler_index].tex_id[0]];
                normal_saved=normal_texture->gxm_tex;
                vglSetTexMipmapCount(&normal_texture->gxm_tex,1);
                vglSetTexMipFilter(&normal_texture->gxm_tex,SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
                break;
            }
        }
        return;
    }
    if (!mode || (mode>5 && mode!=8)) return;
    if (mode==5) { glFinish();return; }
    const char *names[]={"u_WaterSpecularColor","u_FoamIntensity","u_ReflectionFactor","u_WaterNormalIntensity"};
    const char *name=mode==8?"u_ACPNormalBlend":names[mode-1];
    for (unsigned i=0;i<p->frag_uniforms_num;++i) {
        uniform *u=&p->frag_uniforms[i];
        if (u->type==UNIFORM_DATA && u->fptr && !strcmp(sceGxmProgramParameterGetName(u->ptr),name)) {
            components=mode==1?3:1;target=u->fptr;
            memcpy(saved,target,components*sizeof(float));memset(target,0,components*sizeof(float));
            dirty_shader_frag_unifs=GL_TRUE;break;
        }
    }
}

/* Developer-only capture, called before a real ocean draw. The flag is never
 * packaged. Direct bounded writes avoid retaining texture copies in RAM. */
void acp_vitagl_water_trace(void) {
    extern int acp_shader_trace_enabled(void);
    extern void acp_log(const char *, ...);
    extern volatile unsigned acp_water_trace_request;
    static int done;
    if ((done && !__atomic_load_n(&acp_water_trace_request,__ATOMIC_ACQUIRE)) || !cur_program || !acp_shader_trace_enabled()) return;
    program *p = &progs[cur_program - 1];
    if (!sceGxmProgramFindParameterByName(p->vshader->prog, "u_WaterColorAttenuationRatio")) return;
    __atomic_store_n(&acp_water_trace_request,0,__ATOMIC_RELEASE);
    done = 1;
    glFinish();
    for (unsigned stage=0; stage<2; ++stage) {
        uniform *list=stage?p->frag_uniforms:p->vert_uniforms;
        unsigned count=stage?p->frag_uniforms_num:p->vert_uniforms_num;
        for (unsigned i=0; i<count; ++i) {
            uniform *u=&list[i];
            const char *name=sceGxmProgramParameterGetName(u->ptr);
            if (u->type==UNIFORM_DATA) {
                unsigned comps=sceGxmProgramParameterGetComponentCount(u->ptr);
                unsigned arrays=sceGxmProgramParameterGetArraySize(u->ptr);
                float *data=stage?u->fptr:u->vptr;
                if (data && sceGxmProgramParameterGetType(u->ptr)==SCE_GXM_PARAMETER_TYPE_F32)
                    acp_log("Ocean uniform stage=%u %s index=%u comps=%u array=%u first=%g,%g,%g,%g",stage,name,sceGxmProgramParameterGetResourceIndex(u->ptr),comps,arrays,data[0],comps>1?data[1]:0.0f,comps>2?data[2]:0.0f,comps>3?data[3]:0.0f);
            } else {
                unsigned unit=u->sampler_index;
                if(unit>=COMBINED_TEXTURE_IMAGE_UNITS_NUM)continue;
                unsigned id=texture_units[unit].tex_id[u->type==UNIFORM_CUBE_SAMPLER?2:0];
                texture *t=&texture_slots[id];
                unsigned w,h;vglGetTexSizes(&t->gxm_tex,&w,&h);
                acp_log("Ocean sampler %s unit=%u tex=%u size=%ux%u format=%x min=%u mag=%u mipfilter=%u mips=%u usemips=%u",name,unit,id,w,h,t->format,t->min_filter,t->mag_filter,t->mip_filter,t->mip_count,t->use_mips);
                unsigned bytes=0;
                if(t->format==SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR)bytes=VGL_ALIGN(w,8)*h*4;
                if(t->format==SCE_GXM_TEXTURE_FORMAT_UBC1_1BGR)bytes=acp_bc1_size(w,h);
                if(t->format==SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_RGBA)bytes=VGL_ALIGN(w,8)*h*2;
                if(bytes && bytes<=4u*1024*1024 && t->data){
                    void *source=t->data;
                    if(!strcmp(name,"u_Reflection") && t->format==SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR){
                        void *readback=gpu_alloc_mapped_temp(bytes);
                        if(readback){
                            int result=sceGxmTransferCopy(w,h,0,0,SCE_GXM_TRANSFER_COLORKEY_NONE,
                                SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,SCE_GXM_TRANSFER_LINEAR,t->data,0,0,VGL_ALIGN(w,8)*4,
                                SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,SCE_GXM_TRANSFER_LINEAR,readback,0,0,VGL_ALIGN(w,8)*4,NULL,0,NULL);
                            sceGxmTransferFinish();
                            if(result==0)source=readback;
                        }
                    }
                    char path[192];snprintf(path,sizeof(path),"ux0:data/acpirates/shader-trace/ocean-%s-%u-%u-%x.raw",name,w,h,t->format);
                    int fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
                    if(fd>=0){
                        unsigned char chunk[4096];
                        for(unsigned off=0;off<bytes;){
                            unsigned n=MIN(sizeof(chunk),bytes-off);
                            memcpy(chunk,(unsigned char *)source+off,n);
                            int result=sceIoWrite(fd,chunk,n);
                            if(result!=(int)n){acp_log("Ocean texture write failed: %s %d",name,result);break;}
                            off+=n;
                        }
                        sceIoClose(fd);
                    }
                }
            }
        }
    }
}
