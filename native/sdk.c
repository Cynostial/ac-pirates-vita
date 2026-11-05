#include "acp_sdk.h"
#include "acp_native.h"
#include <psp2/kernel/rng.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
static acp_sdk sdk;
static int random_bytes(void *p,unsigned char *out,unsigned size){(void)p;return sceKernelGetRandomNumber(out,size)<0?-1:0;}
static void log_message(void *p,const char *text){(void)p;acp_log("MSDK: %s",text);}
static int relative_path(const char *s){
    if(!s||!*s||*s=='/'||strchr(s,':')||strchr(s,'\\'))return 0;
    for(const char *part=s;*part;){
        const char *end=strchr(part,'/');size_t n=end?(size_t)(end-part):strlen(part);
        if((n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.'))return 0;
        if(!end)return 1;
        part=end+1;if(!*part)return 0;
    }
    return 0;
}
static int prepare_file(void *p,const char *asset,const char *destination){
    (void)p;const char *prefix=ACP_DATA "userdata/";
    if(*asset=='/'||*asset=='\\')asset++;
    if(!relative_path(asset)||strncmp(destination,prefix,strlen(prefix))||!relative_path(destination+strlen(prefix)))return -1;
    char source[512],temporary[512];
    if(snprintf(source,sizeof(source),ACP_DATA "assets/%s",asset)>=(int)sizeof(source)||
       snprintf(temporary,sizeof(temporary),"%s.copying",destination)>=(int)sizeof(temporary))return -1;
    FILE *in=fopen(source,"rb");if(!in)return -1;
    FILE *out=fopen(temporary,"wb");if(!out){fclose(in);return -1;}
    unsigned char buffer[8192];int failed=0;size_t bytes;
    while((bytes=fread(buffer,1,sizeof(buffer),in))!=0)if(fwrite(buffer,1,bytes,out)!=bytes){failed=1;break;}
    if(ferror(in))failed=1;
    if(fflush(out)||fsync(fileno(out)))failed=1;
    if(fclose(out))failed=1;
    fclose(in);
    if(!failed&&rename(temporary,destination))failed=1;
    if(failed)remove(temporary);
    return failed?-1:0;
}
int acp_sdk_vita_configure(acp_platform *platform,acp_jni_registry *registry){
    const char *path=ACP_DATA "userdata/installation.id";char id[37]={0};
    FILE *f=fopen(path,"rb");
    if(f){size_t bytes=fread(id,1,36,f);int extra=fgetc(f);fclose(f);if(bytes!=36||extra!=EOF)return -1;}
    else{
        if(errno!=ENOENT)return -1;
        unsigned char bytes[16];if(random_bytes(NULL,bytes,sizeof(bytes))||acp_sdk_uuid(bytes,id))return -1;
        const char *temporary=ACP_DATA "userdata/installation.id.copying";
        f=fopen(temporary,"wb");if(!f)return -1;
        int failed=fwrite(id,1,36,f)!=36;if(fflush(f)||fsync(fileno(f)))failed=1;if(fclose(f))failed=1;
        if(!failed&&rename(temporary,path))failed=1;
        if(failed){remove(temporary);return -1;}
    }
    acp_sdk_ops ops={NULL,random_bytes,prepare_file,log_message,id};
    return acp_sdk_configure(&sdk,platform,registry,&ops);
}
