#include "acp_sparse_cache.h"
#include <string.h>
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int acp_sparse_cache_init(acp_sparse_cache *c,const void *data,size_t size,uint32_t source){
    memset(c,0,sizeof(*c));
    if(!data||size<24||size>ACP_CACHE_LIMIT)return -1;
    const unsigned char *b=data;
    if(memcmp(b,"ACPCACH1",8)||u32(b+8)!=source||u32(b+20)!=size)return -1;
    uint32_t count=u32(b+12),payload=u32(b+16);
    if(!count||count>(size-24)/12||payload!=24+count*12)return -1;
    uint32_t end=0,cursor=payload;
    for(uint32_t i=0;i<count;i++){
        const unsigned char *r=b+24+i*12;
        uint32_t offset=u32(r),length=u32(r+4),at=u32(r+8);
        if(offset<end||offset>source||!length||length>source-offset||at!=cursor||length>size-cursor)return -1;
        end=offset+length;cursor+=length;
    }
    if(cursor!=size)return -1;
    c->bytes=b;c->count=count;c->source_bytes=source;return 0;
}
size_t acp_sparse_cache_span(const acp_sparse_cache *c,uint32_t offset,size_t limit,const unsigned char **data){
    *data=NULL;
    if(!c||!c->bytes||offset>=c->source_bytes||!limit)return 0;
    uint32_t lo=0,hi=c->count;
    while(lo<hi){uint32_t m=lo+(hi-lo)/2;if(u32(c->bytes+24+m*12)<=offset)lo=m+1;else hi=m;}
    uint32_t end=c->source_bytes;
    if(lo){
        const unsigned char *r=c->bytes+24+(lo-1)*12;
        uint32_t start=u32(r),length=u32(r+4);
        if(offset-start<length){end=start+length;*data=c->bytes+u32(r+8)+offset-start;}
    }
    if(!*data&&lo<c->count)end=u32(c->bytes+24+lo*12);
    size_t available=end-offset;return available<limit?available:limit;
}
