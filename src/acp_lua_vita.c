#include "acp_lua_vita.h"
#include "tlsf/tlsf.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static _Noreturn void fatal(void){sceKernelExitProcess(1);abort();}
static int wait_heap(void *p){return sceKernelWaitSema(((acp_lua_vita*)p)->semaphore,1,NULL);}
static int signal_heap(void *p){return sceKernelSignalSema(((acp_lua_vita*)p)->semaphore,1);}
static void lock(void *p){
    acp_lua_vita *v=p;int waited=acp_fast_mutex_lock(&v->mutex,wait_heap,v);if(waited<0)fatal();
    if(waited)v->stats.lock_wait++;else v->stats.lock_fast++;
}
static void unlock(void *p){acp_lua_vita *v=p;if(acp_fast_mutex_unlock(&v->mutex,signal_heap,v)<0)fatal();}
static acp_lua_vita *active;
static void *resize(void *p,void *old,size_t bytes){
    acp_lua_vita *v=p;v->stats.resize_calls++;
    if(v->resize_tick++&255u)return tlsf_realloc(v->pool,old,bytes);
    uint64_t begin=sceKernelGetProcessTimeWide();
    void *result=tlsf_realloc(v->pool,old,bytes);
    v->stats.resize_us+=sceKernelGetProcessTimeWide()-begin;v->stats.resize_samples++;
    return result;
}
static void release(void *p,void *old){
    acp_lua_vita *v=p;v->stats.free_calls++;
    if(v->free_tick++&255u){tlsf_free(v->pool,old);return;}
    uint64_t begin=sceKernelGetProcessTimeWide();tlsf_free(v->pool,old);
    v->stats.free_us+=sceKernelGetProcessTimeWide()-begin;v->stats.free_samples++;
}
extern void acp_log(const char *,...);
static size_t extend(void *p,size_t bytes){
    acp_lua_vita *v=p;
    if(v->extensions>=ACP_LUA_VITA_EXTENSIONS)return 0;
    /* TLSF's good-fit search rounds a request up by up to 1/32 of its size. */
    size_t need=bytes+bytes/16u+tlsf_pool_overhead()+tlsf_alloc_overhead()+4096u;
    size_t size=need<=ACP_LUA_VITA_EXTENSION_BYTES?ACP_LUA_VITA_EXTENSION_BYTES:(need+65535u)&~(size_t)65535u;
    SceKernelFreeMemorySizeInfo info={.size=sizeof(info)};
    if(sceKernelGetFreeMemorySize(&info)<0||info.size_user<0||(size_t)info.size_user<size+ACP_LUA_VITA_FREE_FLOOR){
        acp_log("Lua arena: %u KiB request refused; free main %d KiB is at the system floor",
            (unsigned)(bytes/1024),info.size_user/1024);
        return 0;
    }
    SceUID block=sceKernelAllocMemBlock("ACP Lua extension",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,size,NULL);
    void *base=NULL;
    if(block<0)return 0;
    if(sceKernelGetMemBlockBase(block,&base)<0||!tlsf_add_pool(v->pool,base,size)){sceKernelFreeMemBlock(block);return 0;}
    v->extension[v->extensions++]=block;
    acp_log("Lua arena: extended by %u KiB for a %u KiB request (%u of %u extensions); free main was %d KiB",
        (unsigned)(size/1024),(unsigned)(bytes/1024),v->extensions,ACP_LUA_VITA_EXTENSIONS,info.size_user/1024);
    return size;
}
/* Big transient game-heap blocks (texture decode/flip buffers) that the
 * fragmented game heap cannot place borrow free space here (native/memory.c).
 * They are charged to this heap while held and only taken while at least
 * ACP_LUA_VITA_LEND_RESERVE more stays available to the scripts. */
void *acp_lua_vita_lend(size_t alignment,size_t bytes){
    acp_lua_vita *v=active;void *p=NULL;
    if(!v)return NULL;
    lock(v);acp_lua_heap *h=&v->heap;
    if(h->used<=h->limit&&bytes<=h->limit-h->used&&h->limit-h->used-bytes>=ACP_LUA_VITA_LEND_RESERVE){
        p=tlsf_memalign(v->pool,alignment<16?16:alignment,bytes);
        if(p){h->used+=tlsf_block_size(p);if(h->used>h->peak)h->peak=h->used;}
    }
    unlock(v);return p;
}
void acp_lua_vita_reclaim(void *p){
    acp_lua_vita *v=active;
    lock(v);v->heap.used-=tlsf_block_size(p);tlsf_free(v->pool,p);unlock(v);
}
static void largest_free(void *ptr,size_t size,int used,void *user){(void)ptr;if(!used&&size>*(size_t*)user)*(size_t*)user=size;}
size_t acp_lua_vita_largest_free(void){
    acp_lua_vita *v=active;size_t largest=0;
    if(!v)return 0;
    lock(v);tlsf_walk_pool(tlsf_get_pool(v->pool),largest_free,&largest);
    for(unsigned i=0;i<v->extensions;i++){
        void *base=NULL;
        if(sceKernelGetMemBlockBase(v->extension[i],&base)>=0)tlsf_walk_pool(base,largest_free,&largest);
    }
    unlock(v);return largest;
}
int acp_lua_vita_sample(acp_lua_heap *heap,acp_lua_backend_stats *out){
    if(!active||heap!=&active->heap||!out)return -1;
    lock(active);*out=active->stats;active->stats=(acp_lua_backend_stats){0};unlock(active);return 0;
}
int acp_lua_vita_create(acp_lua_vita *v,size_t bytes){
    if(active||!v||bytes<65536||bytes>96u*1024*1024||(bytes&65535))return -1;
    memset(v,0,sizeof(*v));v->block=-1;
    v->semaphore=sceKernelCreateSema("ACP Lua heap",0,0,INT_MAX,NULL);
    if(v->semaphore<0)return -1;
    v->mutex_ready=1;
    v->block=sceKernelAllocMemBlock("ACP Lua backing",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,bytes,NULL);
    if(v->block<0){sceKernelDeleteSema(v->semaphore);v->mutex_ready=0;return -1;}
    v->backing=bytes;
    if(sceKernelGetMemBlockBase(v->block,&v->arena)<0)goto failed;
    /* TLSF's control structure and pool both live inside this same reservation.
     * The benaphore serializes allocation from every Lua state/GC worker. */
    v->pool=tlsf_create_with_pool(v->arena,bytes);if(!v->pool)goto failed;
    acp_lua_heap_ops ops={v,lock,unlock,resize,release};
    if(acp_lua_heap_init(&v->heap,bytes,&ops))goto failed;
    v->heap.grow=extend;
    active=v;return 0;
failed:
    if(v->pool){tlsf_destroy(v->pool);v->pool=NULL;}
    if(sceKernelFreeMemBlock(v->block)>=0){v->block=-1;v->backing=0;}
    if(sceKernelDeleteSema(v->semaphore)>=0)v->mutex_ready=0;
    return -1;
}
int acp_lua_vita_destroy(acp_lua_vita *v){
    if(!v||v->heap.blocks||!acp_fast_mutex_idle(&v->mutex))return -1;
    if(v->pool){if(tlsf_check(v->pool)||tlsf_check_pool(tlsf_get_pool(v->pool)))return -1;tlsf_destroy(v->pool);v->pool=NULL;}
    while(v->extensions){if(sceKernelFreeMemBlock(v->extension[v->extensions-1])<0)return -1;v->extensions--;}
    if(v->block>=0){if(sceKernelFreeMemBlock(v->block)<0)return -1;v->block=-1;v->backing=0;}
    if(v->mutex_ready){if(sceKernelDeleteSema(v->semaphore)<0)return -1;v->mutex_ready=0;}
    if(active==v)active=NULL;
    return 0;
}
