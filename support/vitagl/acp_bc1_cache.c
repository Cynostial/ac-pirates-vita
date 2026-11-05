#include "acp_bc1.h"
#include "acp_texture_cache.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
extern void acp_log(const char *,...);
void acp_etc1_to_bc1_cached(const uint8_t *src,uint8_t *dst,unsigned w,unsigned h){
    const char *dir="ux0:data/acpirates/textures-bc1-v1";
    size_t bytes=acp_bc1_size(w,h);
    if(!src||bytes<32768||bytes>ACP_TEXTURE_CACHE_MAX){acp_etc1_to_bc1(src,dst,w,h);return;}
    static unsigned hits,misses,write_failures;
    static uint64_t converted_us,hit_us;
    uint64_t start=sceKernelGetProcessTimeWide();char key[65];
    acp_texture_cache_key(src,(size_t)((w+3)/4)*((h+3)/4)*8,w,h,key);
    if(acp_texture_cache_read(dir,key,dst,bytes)){
        ++hits;hit_us+=sceKernelGetProcessTimeWide()-start;
    }else{
        uint64_t convert=sceKernelGetProcessTimeWide();
        acp_etc1_to_bc1(src,dst,w,h);converted_us+=sceKernelGetProcessTimeWide()-convert;++misses;
        sceIoMkdir(dir,0777);
        if(acp_texture_cache_write(dir,key,dst,bytes))++write_failures;
    }
    if(hits+misses<=4||!((hits+misses)%16))acp_log("BC1 cache: hits=%u misses=%u convert=%llu us reads=%llu us write_failures=%u",
        hits,misses,(unsigned long long)converted_us,(unsigned long long)hit_us,write_failures);
}
/* Hardware path: original ETC1 blocks, swizzled straight into GPU memory. */
void acp_etc1_native_upload(unsigned w,unsigned h,int level){
    static unsigned uploads;static uint64_t bytes;
    if(level)return;
    ++uploads;bytes+=(uint64_t)((w+3)/4)*((h+3)/4)*8;
    if(uploads<=4||!(uploads%64))acp_log("ETC1 native: uploads=%u bytes=%llu",uploads,(unsigned long long)bytes);
}
