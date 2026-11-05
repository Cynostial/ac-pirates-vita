/* Bounded exclusive wall-time samples. Hardware uses 32 frames per session,
 * one in 256 after warmup. Detailed import/JIT profiling stays private. */
#include "acp_native.h"
#include <psp2/kernel/processmgr.h>
#include <string.h>
#ifdef ACP_UPDATE_SAMPLES
#define METHODS 512
static unsigned samples_taken;
#else
#define METHODS 2048
#endif
static struct {char name[96];uint64_t total;unsigned count;} methods[METHODS];
static struct {uint64_t start,children;int id;} stack[256];
static unsigned count,depth,missed,frames,rejected;
static _Thread_local int active;
extern volatile unsigned acp_presented_frames;
int acp_method_profiling(void){return count!=0;}
int acp_method_register(const char *name){
    for(unsigned i=0;i<count;i++)if(!strcmp(name,methods[i].name))return (int)i;
    if(count==METHODS){rejected++;return -1;}
    strncpy(methods[count].name,name,sizeof(methods[count].name)-1);
    return (int)count++;
}
int acp_method_frame(int begin){
    if(!count)return 0;
    if(begin){
        depth=0;
#ifdef ACP_UPDATE_SAMPLES
        active=samples_taken<32 && acp_presented_frames>=512 && !(acp_presented_frames&255u);
        if(active)samples_taken++;
#else
        active=!(acp_presented_frames&63u);
#endif
        if(active)frames++;
    }
    else {if(depth)missed++;depth=0;active=0;}
    return active;
}
int acp_method_phase(int id,int begin){
    if(!active||id<0||(unsigned)id>=count)return 0;
    if(begin){
        if(depth==256){missed++;return 0;}
        stack[depth].id=id;stack[depth].start=sceKernelGetProcessTimeWide();
        stack[depth++].children=0;return 1;
    }
    if(!depth||stack[depth-1].id!=id){missed++;depth=0;active=0;return 0;}
    --depth;uint64_t elapsed=sceKernelGetProcessTimeWide()-stack[depth].start;
    methods[id].total+=elapsed-stack[depth].children;methods[id].count++;
    if(depth)stack[depth-1].children+=elapsed;
    return 0;
}
#ifdef ACP_DEVELOPMENT_PROFILE
int acp_import_begin(unsigned index){
    static int ids[6]={-1,-1,-1,-1,-1,-1};
    static const char *names[]={"import:mutex-lock","import:mutex-unlock","import:get-specific","import:clock","native:game-realloc","native:game-free"};
    if(!active||index>=6)return -1;
    if(ids[index]<0)ids[index]=acp_method_register(names[index]);
    return acp_method_phase(ids[index],1)?ids[index]:-1;
}
void acp_import_end(int id){if(id>=0)acp_method_phase(id,0);}
#endif
void acp_method_report(void){
    if(!count||!frames)return;
#ifdef ACP_UPDATE_SAMPLES
    acp_log("Update sample: frames=%u missed=%u registered=%u rejected=%u session=%u/32",frames,missed,count,rejected,samples_taken);
#else
    acp_log("Method profile: sampled-frames=%u missed=%u registered=%u rejected=%u",frames,missed,count,rejected);
#endif
    for(unsigned n=0;n<32;n++){
        unsigned best=0;
        for(unsigned i=1;i<count;i++)if(methods[i].total>methods[best].total)best=i;
        if(!methods[best].total)break;
        acp_log("Method self: %s total=%u us n=%u",methods[best].name,(unsigned)methods[best].total,methods[best].count);
        methods[best].total=0;
    }
    for(unsigned i=0;i<count;i++){methods[i].total=0;methods[i].count=0;}
    frames=missed=0;
}
