#include "acp_native.h"
#include "acp_platform.h"
#include "acp_cache_file.h"
#include "startup_cache.h"
#include <psp2/kernel/threadmgr.h>
#include <string.h>
#include "cache_member_offsets.inc"
#include "obb_id.h"
static SceUID mutex=-1;
static int copy(void *ctx,const char *source,const char *name,char *out,size_t capacity){
    (void)ctx;
    if(sceKernelLockMutex(mutex,1,NULL)<0)return -1;
    FILE *f=fopen(ACP_DATA ACP_OBB,"rb");int result=-1;
    if(f){
        acp_cache_file *cookie=NULL;
        FILE *filtered=acp_startup_cache_file(f,&cookie);
        if(filtered)f=filtered;
        /* Hints belong to one archive's directory; a stale one costs a full scan. */
        int plain=acp_obb_bytes==ACP_OBB_PLAIN_BYTES;
        const cache_member_offset *hints=plain?cache_member_offsets_plain:cache_member_offsets_original;
        unsigned count=plain?sizeof(cache_member_offsets_plain)/sizeof(*hints):sizeof(cache_member_offsets_original)/sizeof(*hints);
        uint32_t hint=UINT32_MAX;
        for(unsigned i=0;i<count;i++)
            if(!strcmp(source,hints[i].name)){hint=hints[i].offset;break;}
        result=acp_copy_cache_member_hint(f,ACP_DATA "userdata/cache",source,name,out,capacity,hint);
        fclose(f);
    }
    acp_log("Cache asset %s -> %s: %s",source,name,result==1?"reused":result==0?"copied":"unavailable");
    sceKernelUnlockMutex(mutex,1);return result;
}
int acp_cache_vita_configure(acp_platform *p){
    mutex=sceKernelCreateMutex("acp_cache_copy",0,0,NULL);if(mutex<0)return -1;
    p->cache_file=copy;return 0;
}
