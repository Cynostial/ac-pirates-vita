#include "acp_lua.h"
#include <string.h>
#include <limits.h>
typedef union {
    struct {size_t bytes;uintptr_t owner;} info;
    uint64_t alignment; /* LuaJIT's TValue ABI requires eight-byte alignment. */
} allocation;
static acp_lua_heap *heap;static acp_lua_api api;
static int enable_jit;
void acp_lua_enable_jit(int enabled){enable_jit=!!enabled;}
int acp_lua_heap_init(acp_lua_heap *h,size_t limit,const acp_lua_heap_ops *ops){
    if(!h||!ops||!ops->lock||!ops->unlock||!ops->resize||!ops->free||limit<4096)return -1;
    *h=(acp_lua_heap){.ops=*ops,.limit=limit,.cache_limit=limit/32u};
    if(h->cache_limit>2u*1024u*1024u)h->cache_limit=2u*1024u*1024u;
    return 0;
}
static size_t charge(size_t bytes){
    if(bytes>SIZE_MAX-sizeof(allocation))return 0;
    size_t n=bytes+sizeof(allocation);return n<=ACP_LUA_SMALL_MAX?(n+15u)&~(size_t)15u:n;
}
static void trim(acp_lua_heap *h){
    for(unsigned i=0;i<ACP_LUA_SMALL_CLASSES;i++)while(h->small[i]){
        void *p=h->small[i];h->small[i]=*(void **)p;h->ops.free(h->ops.ctx,p);
    }
    h->cached=0;
    memset(h->cache_recent,0,sizeof(h->cache_recent));
    memset(h->cache_counts,0,sizeof(h->cache_counts));
}
static void make_cache_room(acp_lua_heap *h,unsigned incoming){
    /* A burst of rarely used GUI sizes must not monopolize the cache while
     * thousands of short-lived gameplay objects fall through to TLSF. Give
     * recently requested classes one second chance, then release up to 32 KiB
     * from a different class. Work is bounded; cached/live bytes remain inside
     * the same fixed arena. Never churn the class we are currently recycling. */
    if((size_t)h->cache_counts[incoming]*(incoming+1u)*16u==h->cached)return;
    for(unsigned scan=0;scan<2u*ACP_LUA_SMALL_CLASSES;scan++){
        unsigned bin=h->cache_hand++%ACP_LUA_SMALL_CLASSES;
        if(bin==incoming||!h->small[bin])continue;
        if(h->cache_recent[bin]){h->cache_recent[bin]=0;continue;}
        size_t bytes=(bin+1u)*16u,released=0;
        while(h->small[bin]&&released<32u*1024u){
            void *p=h->small[bin];h->small[bin]=*(void **)p;
            h->ops.free(h->ops.ctx,p);h->cached-=bytes;released+=bytes;h->cache_evicted++;
            h->cache_counts[bin]--;
        }
        return;
    }
}
static void recycle(acp_lua_heap *h,allocation *a,size_t n){
    if(n<=ACP_LUA_SMALL_MAX&&h->cache_limit&&h->cached+n>h->cache_limit)
        make_cache_room(h,(unsigned)n/16u-1u);
    if(n<=ACP_LUA_SMALL_MAX && h->cached+n<=h->cache_limit && h->used+h->cached+n<=h->limit){
        unsigned bin=(unsigned)n/16u-1u;
        a->info.owner=0;
        *(void **)a=h->small[bin];h->small[bin]=a;h->cached+=n;
        h->cache_counts[bin]++;
    }else h->ops.free(h->ops.ctx,a);
}
/* Last resort before LuaJIT raises "not enough memory": cached blocks go back
 * to the arena first, then the backend may add backing. */
static int grow(acp_lua_heap *h,size_t bytes){
    if(!h->grow)return 0;
    trim(h);
    size_t added=h->grow(h->ops.ctx,bytes);
    if(!added||added>SIZE_MAX-h->limit)return 0;
    h->limit+=added;h->grown+=added;return 1;
}
void *acp_lua_allocate(void *opaque,void *ptr,size_t old_size,size_t new_size){
    acp_lua_heap *h=opaque;if(!h)return NULL;
    h->ops.lock(h->ops.ctx);allocation *a=ptr?(allocation*)ptr-1:NULL;
    size_t old=0;
    if(a){
        if(a->info.owner!=(uintptr_t)h||a->info.bytes!=old_size||!h->blocks||
           !charge(a->info.bytes)||charge(a->info.bytes)>h->used){
            h->failures++;h->ops.unlock(h->ops.ctx);return NULL;
        }
        old=charge(a->info.bytes);
    }
    if(!new_size){
        if(a){h->used-=old;h->blocks--;recycle(h,a,old);if(!h->blocks)trim(h);}
        h->ops.unlock(h->ops.ctx);return NULL;
    }
    size_t bytes=charge(new_size);
    if(!bytes||h->used-old>h->limit||bytes>h->limit-(h->used-old)){
        if(!bytes||!grow(h,bytes)||h->used-old>h->limit||bytes>h->limit-(h->used-old)){
            h->failures++;h->last_failure=new_size;h->ops.unlock(h->ops.ctx);return NULL;
        }
    }
    if(a && bytes==old && bytes<=ACP_LUA_SMALL_MAX){a->info.bytes=new_size;h->cache_hits++;h->ops.unlock(h->ops.ctx);return ptr;}
    allocation *b=NULL;
    unsigned bin=bytes<=ACP_LUA_SMALL_MAX?(unsigned)bytes/16u-1u:0;
    if(bytes<=ACP_LUA_SMALL_MAX)h->cache_recent[bin]=1;
    if(bytes<=ACP_LUA_SMALL_MAX && h->small[bin]){
        b=h->small[bin];h->small[bin]=*(void **)b;h->cached-=bytes;h->cache_hits++;
        h->cache_counts[bin]--;
        if(a)memcpy(b+1,a+1,new_size<old_size?new_size:old_size);
        h->used=h->used-old+bytes;
        if(a)recycle(h,a,old);
    }else{
        if(h->cached>h->limit-(h->used-old)-bytes)trim(h);
        h->backend_calls++;
#ifdef ACP_DEVELOPMENT_PROFILE
        if(bytes<=ACP_LUA_SMALL_MAX)h->cache_misses[bin]++;
#endif
        b=h->ops.resize(h->ops.ctx,a,bytes);
        if(!b&&h->cached){trim(h);h->backend_calls++;b=h->ops.resize(h->ops.ctx,a,bytes);}
        /* Fragmentation can refuse a request the limit admits. */
        if(!b&&grow(h,bytes)){h->backend_calls++;b=h->ops.resize(h->ops.ctx,a,bytes);}
        if(b)h->used=h->used-old+bytes;
    }
    if(!b){h->failures++;h->last_failure=new_size;h->ops.unlock(h->ops.ctx);return NULL;}
    b->info.bytes=new_size;b->info.owner=(uintptr_t)h;
    if(h->used>h->peak)h->peak=h->used;
    if(!a)h->blocks++;
    if(h->pressure&&h->limit-h->used<=h->pressure_headroom)h->pressure(h->pressure_ctx);
    h->ops.unlock(h->ops.ctx);return b+1;
}
static int prepare_status;
static int open_libraries(lua_State *state){api.openlibs(state);prepare_status=api.prepare?api.prepare(state):0;return 0;}
int acp_lua_install(acp_lua_heap *h,const acp_lua_api *a){
    if(heap||!h||!a||!a->newstate||!a->atpanic||!a->openlibs||!a->setmode||!a->close||!a->cpcall||!a->panic)return -1;
    heap=h;api=*a;return 0;
}
lua_State *acp_lua_create_state(void){
    if(!heap)return NULL;
    lua_State *state=api.newstate(acp_lua_allocate,heap);if(!state)return NULL;
    api.atpanic(state,api.panic);
    /* LUAJIT_MODE_ENGINE == 0 and LUAJIT_MODE_OFF == 0 in the pinned LuaJIT 2.0.
     * No Lua chunk or game callback executes before disabling compilation. */
    prepare_status=0;
    if(!api.setmode(state,0,0)||api.cpcall(state,open_libraries,NULL)||prepare_status||!api.setmode(state,0,enable_jit?0x100:0)){
        api.close(state);return NULL;
    }
    return state;
}
