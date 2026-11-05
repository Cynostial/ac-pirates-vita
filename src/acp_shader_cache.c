#include "acp_shader_cache.h"
#include "acp_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const unsigned char magic[8]={'A','C','P','S','H','C','1',0};
static void digest(const void *p,size_t n,unsigned char out[32]){
    acp_sha256 ctx;acp_sha256_init(&ctx);acp_sha256_update(&ctx,p,n);acp_sha256_final(&ctx,out);
}
void acp_shader_cache_key(const void *source,size_t n,const void *settings,size_t sn,char key[65]){
    static const char version[]="ACP GXP v1; VitaGL 9c23758; translated GLSL; no textureSize";
    acp_sha256 ctx;unsigned char hash[32];acp_sha256_init(&ctx);
    acp_sha256_update(&ctx,version,sizeof(version));acp_sha256_update(&ctx,settings,sn);
    acp_sha256_update(&ctx,source,n);acp_sha256_final(&ctx,hash);
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){key[2*i]=hex[hash[i]>>4];key[2*i+1]=hex[hash[i]&15];}key[64]=0;
}
static int path(char out[512],const char *directory,const char *key,const char *suffix){
    if(!directory||!key||strlen(key)!=64)return -1;
    for(unsigned i=0;i<64;i++)if(!((key[i]>='0'&&key[i]<='9')||(key[i]>='a'&&key[i]<='f')))return -1;
    int n=snprintf(out,512,"%s/%s%s",directory,key,suffix);return n<0||n>=512?-1:0;
}
size_t acp_shader_cache_read(const char *directory,const char *key,void **payload){
    if(!payload)return 0;
    *payload=NULL;char name[512];if(path(name,directory,key,".bin"))return 0;
    FILE *f=fopen(name,"rb");if(!f)return 0;
    setvbuf(f,NULL,_IONBF,0);
    unsigned char header[44],hash[32];void *p=NULL;size_t size=0;
    if(fread(header,1,sizeof(header),f)!=sizeof(header)||memcmp(header,magic,8))goto done;
    size=(size_t)header[8]|((size_t)header[9]<<8)|((size_t)header[10]<<16)|((size_t)header[11]<<24);
    if(!size||size>ACP_SHADER_CACHE_MAX){size=0;goto done;}
    p=malloc(size);if(!p){size=0;goto done;}
    if(fread(p,1,size,f)!=size||fgetc(f)!=EOF||ferror(f)){size=0;goto done;}
    digest(p,size,hash);if(memcmp(hash,header+12,32))size=0;
done:
    fclose(f);if(!size){free(p);p=NULL;}*payload=p;return size;
}
int acp_shader_cache_write(const char *directory,const char *key,const void *payload,size_t size){
    char name[512],temp[512];
    if(!payload||!size||size>ACP_SHADER_CACHE_MAX||path(name,directory,key,".bin")||path(temp,directory,key,".tmp"))return -1;
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    unsigned char header[44];memcpy(header,magic,8);
    for(unsigned i=0;i<4;i++)header[8+i]=(unsigned char)(size>>(i*8));
    digest(payload,size,header+12);
    int failed=fwrite(header,1,sizeof(header),f)!=sizeof(header)||fwrite(payload,1,size,f)!=size;
    if(fclose(f))failed=1;
    if(!failed){remove(name);if(rename(temp,name))failed=1;}
    if(failed)remove(temp);
    return failed?-1:0;
}
