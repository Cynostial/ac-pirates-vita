#include "acp_native.h"
#include "acp_platform.h"
#include <stdio.h>
#include <string.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>

int acp_shader_trace_enabled(void){
    static int enabled=-1;
    if(enabled<0){
        int fd=sceIoOpen(ACP_DATA "shader-trace.flag",SCE_O_RDONLY,0);
        enabled=fd>=0;
        if(fd>=0){sceIoClose(fd);sceIoMkdir(ACP_DATA_PATH "/shader-trace",0777);}
    }
    return enabled;
}
/* Opt-in, bounded disk capture of the real linked shader pair. No persistent
 * copies of sources or textures are retained in RAM. The flag is not shipped. */
void acp_shader_trace(const char *source,unsigned id,unsigned type,int translated){
    static size_t written;
    if(!source||!acp_shader_trace_enabled())return;
    size_t n=strnlen(source,256u*1024);
    if(n==256u*1024||n>16u*1024*1024-written)return;
    char path[160];
    snprintf(path,sizeof(path),ACP_DATA "shader-trace/%u-%x-%s.txt",id,type,translated?"cg":"glsl");
    int fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    if(fd>=0){int bytes=sceIoWrite(fd,source,n);if(bytes>0)written+=(unsigned)bytes;sceIoClose(fd);}
}

/* Bounded captures from the real shader compiler, not a separate test app. */
void acp_shader_failure(const char *source,unsigned type){
    static unsigned failures;
    unsigned id=++failures;
    acp_log("Shader compiler rejected stage 0x%x (failure %u)",type,id);
    if(!source||id>8)return;
    size_t bytes=strnlen(source,256u*1024);
    char path[128];
    snprintf(path,sizeof(path),ACP_DATA "failed-shader-%u-%x.cg",id,type);
    FILE *file=fopen(path,"wb");
    if(file){fwrite(source,1,bytes,file);fclose(file);}
}
