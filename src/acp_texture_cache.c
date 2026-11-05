#include "acp_texture_cache.h"
#include "acp_sha256.h"
#include <stdio.h>
#include <string.h>
static void hash(const void *p,size_t n,unsigned char out[32]){
    acp_sha256 c;acp_sha256_init(&c);acp_sha256_update(&c,p,n);acp_sha256_final(&c,out);
}
void acp_texture_cache_key(const void *source,size_t n,unsigned width,unsigned height,char out[65]){
    static const char version[]="ACP ETC1-BC1 v1; GXM y-first swizzle; VitaGL 9c23758; STB HIGHQUAL";
    acp_sha256 c;unsigned char d[32],size[8];
    for(unsigned i=0;i<4;i++){size[i]=(unsigned char)(width>>(8*i));size[i+4]=(unsigned char)(height>>(8*i));}
    acp_sha256_init(&c);acp_sha256_update(&c,version,sizeof(version));acp_sha256_update(&c,size,8);
    acp_sha256_update(&c,source,n);acp_sha256_final(&c,d);
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){out[2*i]=hex[d[i]>>4];out[2*i+1]=hex[d[i]&15];}out[64]=0;
}
static int path(char out[512],const char *dir,const char *key,const char *suffix){
    if(!dir||!key||strlen(key)!=64)return -1;
    for(unsigned i=0;i<64;i++)if(!((key[i]>='0'&&key[i]<='9')||(key[i]>='a'&&key[i]<='f')))return -1;
    int n=snprintf(out,512,"%s/%s%s",dir,key,suffix);return n<0||n>=512?-1:0;
}
int acp_texture_cache_read(const char *dir,const char *key,void *out,size_t bytes){
    char name[512];if(!out||!bytes||bytes>ACP_TEXTURE_CACHE_MAX||path(name,dir,key,".bin"))return 0;
    FILE *f=fopen(name,"rb");if(!f)return 0;
    /* The explicit 16 KiB CPU chunk below is already our read buffer. */
    setvbuf(f,NULL,_IONBF,0);
    unsigned char h[44],d[32];int ok=0;
    if(fread(h,1,44,f)!=44||memcmp(h,"ACPTXC1\0",8))goto done;
    size_t size=(size_t)h[8]|((size_t)h[9]<<8)|((size_t)h[10]<<16)|((size_t)h[11]<<24);
    if(size!=bytes)goto done;
    /* CDRAM is not CPU cacheable. Hash in a small cached CPU buffer before
     * copying to the GPU destination, rather than repeatedly reading CDRAM. */
    unsigned char chunk[16384];acp_sha256 digest;acp_sha256_init(&digest);
    for(size_t at=0;at<bytes;){
        size_t n=bytes-at;if(n>sizeof(chunk))n=sizeof(chunk);
        if(fread(chunk,1,n,f)!=n)goto done;
        acp_sha256_update(&digest,chunk,n);memcpy((unsigned char *)out+at,chunk,n);at+=n;
    }
    if(fgetc(f)!=EOF||ferror(f))goto done;
    acp_sha256_final(&digest,d);ok=!memcmp(h+12,d,32);
done:
    fclose(f);return ok;
}
int acp_texture_cache_write(const char *dir,const char *key,const void *data,size_t bytes){
    char name[512],temp[512];
    if(!data||!bytes||bytes>ACP_TEXTURE_CACHE_MAX||path(name,dir,key,".bin")||path(temp,dir,key,".tmp"))return -1;
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    unsigned char h[44];memcpy(h,"ACPTXC1\0",8);
    for(unsigned i=0;i<4;i++)h[8+i]=(unsigned char)(bytes>>(8*i));
    hash(data,bytes,h+12);
    int error=fwrite(h,1,44,f)!=44||fwrite(data,1,bytes,f)!=bytes;
    if(fclose(f))error=1;
    if(!error){remove(name);if(rename(temp,name))error=1;}
    if(error)remove(temp);
    return error?-1:0;
}
