#include "acp_cache_file.h"
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#ifdef _WIN32
#include <io.h>
#define sync_file _commit
#else
#include <unistd.h>
#define sync_file fsync
#endif
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const unsigned char *p){return u16(p)|(uint32_t)u16(p+2)<<16;}
static int read_at(FILE *f,uint32_t at,void *p,size_t bytes){
    return fseek(f,(long)at,SEEK_SET)||fread(p,1,bytes,f)!=bytes?-1:0;
}
static int filename(const char *s){
    return s&&*s&&strcmp(s,".")&&strcmp(s,"..")&&!strpbrk(s,"/\\:");
}
int acp_copy_cache_member_hint(FILE *in,const char *directory,const char *member,const char *name,char *out,size_t capacity,uint32_t hint){
    if(out&&capacity)*out=0;
    if(!in||!directory||!member||!*member||!filename(name)||!out||!capacity)return -1;
    char path[512],temporary[528];size_t member_size=strlen(member);
    int n=snprintf(path,sizeof(path),"%s/%s",directory,name);
    if(n<0||(size_t)n>=sizeof(path)||(size_t)n>=capacity)return -1;
    if(snprintf(temporary,sizeof(temporary),"%s.copying",path)>=(int)sizeof(temporary))return -1;
    if(fseek(in,0,SEEK_END))return -1;
    long end=ftell(in);if(end<22||(uint64_t)end>0x7fffffff)return -1;
    unsigned char h[46],local[30],buffer[8192];
    if(read_at(in,(uint32_t)end-22,h,22)||u32(h)!=0x06054b50||u16(h+4)||u16(h+6)||
       u16(h+8)!=u16(h+10)||u16(h+10)==0xffff||u16(h+20))return -1;
    uint32_t central=u32(h+16),central_size=u32(h+12),cursor=central;
    unsigned count=u16(h+10);
    if((uint64_t)central+central_size!=(uint32_t)end-22)return -1;
    int hinted=hint>=central && (uint64_t)hint+46<=central+central_size;
    for(unsigned pass=0;pass<(hinted?2u:1u);pass++){
    int direct=hinted&&pass==0;
    cursor=direct?hint:central;
    for(unsigned i=0;i<(direct?1u:count);i++){
        if((uint64_t)cursor+46>central+central_size||read_at(in,cursor,h,46)||u32(h)!=0x02014b50){if(direct)break;return -1;}
        unsigned length=u16(h+28),extra=u16(h+30),comment=u16(h+32);
        uint64_t next=(uint64_t)cursor+46+length+extra+comment;
        if(next>central+central_size){if(direct)break;return -1;}
        if(length!=member_size){cursor=(uint32_t)next;continue;}
        if(length>=sizeof(buffer)||fread(buffer,1,length,in)!=length)return -1;
        if(memcmp(buffer,member,length)){cursor=(uint32_t)next;continue;}
        uint32_t size=u32(h+24),crc=u32(h+16),offset=u32(h+42);
        unsigned flags=u16(h+8);
        if(u16(h+10)||flags&1||u16(h+34)||u32(h+20)!=size||
           (uint64_t)offset+30>central||read_at(in,offset,local,30)||u32(local)!=0x04034b50||
           u16(local+6)!=flags||u16(local+8)||u16(local+26)!=length)return -1;
        uint64_t start=(uint64_t)offset+30+length+u16(local+28);
        if(start+size>central||fread(buffer,1,length,in)!=length||memcmp(buffer,member,length))return -1;
        if(!(flags&8)&&(u32(local+14)!=crc||u32(local+18)!=size||u32(local+22)!=size))return -1;
        FILE *existing=fopen(path,"rb");
        if(existing){int same=!fseek(existing,0,SEEK_END)&&ftell(existing)==(long)size;fclose(existing);
            if(same){memcpy(out,path,(size_t)n+1);return 1;}}
        if(fseek(in,(long)start,SEEK_SET))return -1;
        FILE *dest=fopen(temporary,"wb");if(!dest)return -1;
        setvbuf(dest,NULL,_IONBF,0);
        uint32_t remaining=size;uLong actual=crc32(0L,Z_NULL,0);int failed=0;
        while(remaining){
            size_t bytes=remaining>sizeof(buffer)?sizeof(buffer):remaining;
            if(fread(buffer,1,bytes,in)!=bytes||fwrite(buffer,1,bytes,dest)!=bytes){failed=1;break;}
            actual=crc32(actual,buffer,(uInt)bytes);remaining-=(uint32_t)bytes;
        }
        if(remaining||(uint32_t)actual!=crc||ferror(in))failed=1;
        if(fflush(dest)||sync_file(fileno(dest)))failed=1;
        if(fclose(dest))failed=1;
        if(!failed&&rename(temporary,path))failed=1;
        if(failed){remove(temporary);return -1;}
        memcpy(out,path,(size_t)n+1);return 0;
    }
    }
    return -1;
}
int acp_copy_cache_member(FILE *in,const char *directory,const char *member,const char *name,char *out,size_t capacity){
    return acp_copy_cache_member_hint(in,directory,member,name,out,capacity,UINT32_MAX);
}
