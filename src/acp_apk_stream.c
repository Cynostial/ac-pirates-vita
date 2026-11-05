#include "acp_apk_stream.h"
#include <string.h>
#include <limits.h>
const acp_apk_entry *acp_apk_find(const acp_apk_entry *entries,size_t count,const char *name){
    size_t lo=0,hi=count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;int c=strcmp(name,entries[mid].name);if(c==0)return entries+mid;if(c<0)hi=mid;else lo=mid+1;}
    return NULL;
}
int acp_apk_read(acp_apk_cursor *c,void *out,unsigned count,int (*read_at)(void *,void *,unsigned,uint32_t),void *ctx){
    if(!c||!c->entry||!c->entry->stored||(!out&&count)||!read_at||c->position>c->entry->size||count>INT_MAX)return -1;
    unsigned left=c->entry->size-c->position;if(count>left)count=left;
    if(!count)return 0;
    if(c->entry->offset>UINT32_MAX-c->position)return -1;
    int n=read_at(ctx,out,count,c->entry->offset+c->position);
    if(n<0||(unsigned)n>count)return -1;
    c->position+=(unsigned)n;return n;
}
int64_t acp_apk_seek(acp_apk_cursor *c,int64_t position){
    if(!c||!c->entry||position<0||(uint64_t)position>c->entry->size)return -1;
    c->position=(uint32_t)position;return position;
}
