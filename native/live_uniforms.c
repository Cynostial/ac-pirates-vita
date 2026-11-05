/* Pinned Spark 2 material layout. Animated scalar/vector uniforms do not need
 * to reload every texture, render state and uniform of the whole material.
 * Resolve live ownership on each call; never cache resource/object addresses. */
#include "acp_native.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <stdint.h>
#include <string.h>

static int (*original)(void *);
static int (*lua_type_)(void *,int);
static const char *(*lua_string_)(void *,int,size_t *);
static double (*lua_number_)(void *,int);
static void *(*lua_userdata_)(void *,int);
static int (*is_struct)(void *,int,const char *);
static void *(*raw_resource)(const char *,int),*(*live_resource)(const char *,int);
static void *(*resource_data)(void *);
static int (*loading_state)(void *);
static unsigned (*raw_count)(void *),(*live_count)(void *);
static void *(*raw_pass)(void *,unsigned),*(*live_pass)(void *,unsigned);
static unsigned hits,fallbacks;
static unsigned mesh_hits,mesh_fallbacks;
static int (*original_mesh)(void *);
static void *(*check_class)(void *,int,const char *);
static unsigned (*submesh_count)(void *);
static void *(*submesh_get)(void *,unsigned);
static uintptr_t expected_mesh_setter;
#ifdef ACP_DEVELOPMENT_PROFILE
unsigned acp_live_uniforms_enabled=3;
#endif
static uintptr_t word(const void *p,unsigned offset){return *(const uintptr_t *)((const char *)p+offset);}
static int empty_vector(const void *resource,unsigned offset){
    const uintptr_t *v=(const void *)word(resource,offset);
    return v && v[0]==v[1];
}
/* Both original maps are libstdc++ ARM32 rb-trees with string keys. Searching
 * the existing nodes avoids the allocating operator[] used by the engine. */
static void *existing_parameter(void *map,const char *name){
    uintptr_t node=word(map,8);
    while(node){
        int order=strcmp(name,(const char *)word((void *)node,16));
        if(!order)return (void *)(node+20);
        node=word((void *)node,order<0?8:12);
    }
    return NULL;
}
static void *(*live_parameters)(void *);
int acp_live_uniform_write(void *raw,void *live,const char *name,const float *value,unsigned count){
    if(!raw || !live || (count!=1 && count!=2) ||
       loading_state(raw)!=2 || loading_state(live)!=2)return 0;
    /* Preserve the original temporary reference/unload behavior for resources
     * which are no longer owned by a live scene. */
    if(!word(raw,0x14) || !word(live,0x14))return 0;
    const uintptr_t *deps=(const void *)word(raw,0x50);
    if(!deps || deps[1]-deps[0]!=4 || *(uintptr_t *)deps[0]!=(uintptr_t)live ||
       !empty_vector(live,0x50) || !empty_vector(live,0x54))return 0;
    void *raw_data=resource_data(raw),*wrapper=resource_data(live);
    if(!raw_data || !wrapper)return 0;
    void *material=(void *)word(wrapper,0);
    if(!material || raw_count(raw_data)<1 || live_count(material)<1)return 0;
    void *rp=raw_pass(raw_data,0),*lp=live_pass(material,0);
    void *raw_map=(void *)word(rp,4),*live_map=live_parameters(lp);
    if(!raw_map || !live_map)return 0;
    uintptr_t *parameter=existing_parameter(raw_map,name);
    uintptr_t *current=existing_parameter(live_map,name);
    /* Raw parameter type/byte length and live type/element count must agree.
     * New uniforms, arrays, textures and graph/observer changes use Spark. */
    if(!parameter || !current || parameter[2]!=count-1 || parameter[3]!=count*4 ||
       current[1]!=count-1 || current[2]!=1 || !current[3])return 0;
    memcpy(parameter+4,value,count*4);
    memcpy((void *)current[3],value,count*4);
    return 1;
}
static int material_uniform(void *state){
#ifdef ACP_DEVELOPMENT_PROFILE
    if(!(acp_live_uniforms_enabled&1))return original(state);
#endif
    if(lua_type_(state,1)==4 && lua_type_(state,2)==3 && lua_number_(state,2)==1.0 && lua_type_(state,3)==4){
        const char *material=lua_string_(state,1,NULL),*name=lua_string_(state,3,NULL);
        int post=!strcmp(material,"PostFXMaterial") || !strcmp(material,"PostFXMaterialWD");
        int rays=!strcmp(material,"GodRaysPass1Material") || !strcmp(material,"GodRaysPass2Material");
        float values[2];unsigned count=0;
        if((post || rays) && !strcmp(name,"u_SunPosition") && is_struct(state,4,"Vector2")){
            memcpy(values,lua_userdata_(state,4),8);count=2;
        }else if(post && !strcmp(name,"u_GodRaysFactor") && lua_type_(state,4)==3){
            values[0]=(float)lua_number_(state,4);count=1;
        }
        if(count && acp_live_uniform_write(raw_resource(material,0),live_resource(material,0),name,values,count)){
            hits++;return 0;
        }
    }
    fallbacks++;return original(state);
}
/* Preflight every selected pass before writing any value, so a missing or
 * differently typed parameter can fall back without partially modifying a mesh. */
__attribute__((noinline)) int acp_mesh_uniform_write(void *mesh,const char *name,const float *values,unsigned count,int sub,int pass){
    if(!mesh || count<1 || count>4 || word((void *)word(mesh,0),0x44)!=expected_mesh_setter)return 0;
    void *entity=(void *)word(mesh,0x48);
    if(!entity)return 0;
    unsigned subs=submesh_count(entity);
    if(subs>32 || sub < -1 || pass < -1 || (sub>=0 && (unsigned)sub>=subs))return 0;
    void *targets[32];unsigned used=0;
    unsigned first=sub<0?0:(unsigned)sub,last=sub<0?subs:(unsigned)sub+1;
    for(unsigned i=first;i<last;i++){
        void *part=submesh_get(entity,i);
        if(!part)return 0;
        void *(*get_material)(void *)=(void *)word((void *)word(part,0),0x1c);
        void *material=get_material(part);
        if(!material)return 0;
        unsigned passes=live_count(material);
        if(passes>32 || (sub>=0 && pass>=0 && (unsigned)pass>=passes))return 0;
        unsigned begin=sub<0 || pass<0?0:(unsigned)pass;
        unsigned end=sub<0 || pass<0?passes:(unsigned)pass+1;
        for(unsigned j=begin;j<end;j++){
            if(used==32)return 0;
            void *shader_pass=live_pass(material,j);
            void *map=live_parameters(shader_pass);
            uintptr_t *parameter=map?existing_parameter(map,name):NULL;
            if(!parameter || parameter[1]!=count-1 || parameter[2]!=1 || !parameter[3])return 0;
            targets[used++]=(void *)parameter[3];
        }
    }
    for(unsigned i=0;i<used;i++)memcpy(targets[i],values,count*4);
    return 1;
}
static int optional_index(void *state,int arg,int *out){
    int type=lua_type_(state,arg);
    if(type==-1 || type==0){*out=-1;return 1;}
    if(type!=3)return 0;
    double value=lua_number_(state,arg);
    if(!(value>=-1 && value<=32))return 0;
    *out=(int)value;return value==*out;
}
static int mesh_uniform(void *state){
#ifdef ACP_DEVELOPMENT_PROFILE
    if(!(acp_live_uniforms_enabled&2))return original_mesh(state);
#endif
    int sub,pass;float values[4];unsigned count=0;
    if(lua_type_(state,2)!=4 || !optional_index(state,4,&sub) || !optional_index(state,5,&pass))goto fallback;
    if(lua_type_(state,3)==3){values[0]=(float)lua_number_(state,3);count=1;}
    else if(lua_type_(state,3)==7){
        if(is_struct(state,3,"Vector2"))count=2;
        else if(is_struct(state,3,"Vector3"))count=3;
        else if(is_struct(state,3,"Vector4") || is_struct(state,3,"Color"))count=4;
        if(count)memcpy(values,lua_userdata_(state,3),count*4);
    }
    if(count){
        void *mesh=check_class(state,1,"MeshBaseNative");
        if(acp_mesh_uniform_write(mesh,lua_string_(state,2,NULL),values,count,sub,pass)){mesh_hits++;return 0;}
    }
fallback:
    mesh_fallbacks++;return original_mesh(state);
}
void acp_live_uniforms_report(void){
    if(hits || fallbacks)acp_log("Live material uniforms: direct=%u original=%u",hits,fallbacks);
    if(mesh_hits || mesh_fallbacks)acp_log("Live mesh uniforms: direct=%u original=%u",mesh_hits,mesh_fallbacks);
    hits=fallbacks=0;
    mesh_hits=mesh_fallbacks=0;
}
int acp_live_uniforms_install(so_module *m){
    uintptr_t slot=m->load_addr+0x10f5844;
    if(word((void *)slot,0)!=m->load_addr+0x60a74d ||
       word((void *)(slot-4),0)!=m->load_addr+0xf98e4c ||
       strcmp((const char *)(m->load_addr+0xf98e4c),"MaterialResources_SetShaderMaterialParam"))return -1;
    uintptr_t mesh_slot=m->load_addr+0x1108394;
    if(word((void *)mesh_slot,0)!=m->load_addr+0x672575 ||
       word((void *)(mesh_slot-4),0)!=m->load_addr+0xf8d828 ||
       strcmp((const char *)(m->load_addr+0xf8d828),"SetShaderParameter"))return -1;
#define RESOLVE(to,name) do{to=(void *)so_symbol(m,name);if(!to)return -1;}while(0)
    original=(void *)(m->load_addr+0x60a74d);
    RESOLVE(lua_type_,"lua_type");RESOLVE(lua_string_,"lua_tolstring");
    RESOLVE(lua_number_,"lua_tonumber");RESOLVE(lua_userdata_,"lua_touserdata");
    RESOLVE(is_struct,"_ZN13LuaBindTools28IsStructEP9lua_StateiPKc");
    RESOLVE(raw_resource,"_ZN14SparkResources25RawShaderMaterialResource11GetFromNameEPKcb");
    RESOLVE(live_resource,"_ZN13LuaGeeaEngine20GeeaMaterialResource11GetFromNameEPKcb");
    RESOLVE(resource_data,"_ZN14SparkResources8Resource15GetDataInternalEv");
    RESOLVE(loading_state,"_ZNK14SparkResources8Resource15GetLoadingStateEv");
    RESOLVE(raw_count,"_ZNK14SparkResources14ShaderMaterial12GetPassCountEv");
    RESOLVE(live_count,"_ZNK10geMaterial18GetShaderPassCountEv");
    RESOLVE(raw_pass,"_ZN14SparkResources14ShaderMaterial7GetPassEj");
    RESOLVE(live_pass,"_ZN10geMaterial13GetShaderPassEj");
    RESOLVE(live_parameters,"_ZNK12geShaderPass26GetShaderParameterValueMapEv");
    RESOLVE(check_class,"_ZN13LuaBindTools214CheckClassDataEP9lua_StateiPKc");
    RESOLVE(submesh_count,"_ZNK12geMeshEntity21GetSubMeshEntityCountEv");
    RESOLVE(submesh_get,"_ZN12geMeshEntity16GetSubMeshEntityEj");
    expected_mesh_setter=so_symbol(m,"_ZN13LuaGeeaEngine11PakGeeaMesh18SetShaderParameterEjjRKN14SparkResources15ShaderParameterE");
    if(!expected_mesh_setter)return -1;
    original_mesh=(void *)(m->load_addr+0x672575);
#undef RESOLVE
    uintptr_t target=(uintptr_t)material_uniform;
    if(kuKernelCpuUnrestrictedMemcpy((void *)slot,&target,4)<0)return -1;
    target=(uintptr_t)mesh_uniform;
    if(kuKernelCpuUnrestrictedMemcpy((void *)mesh_slot,&target,4)<0)return -1;
    acp_log("Live material uniforms: guarded in-place raw/live updates; original resource graph fallback");
    return 0;
}
