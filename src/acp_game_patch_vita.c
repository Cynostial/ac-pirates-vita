#include "acp_game_patch.h"
#include "acp_lua.h"
#include "acp_lua_vita.h"
#include "acp_gc.h"
#include <so_util/so_util.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <kubridge.h>
#include <string.h>
extern int acp_vita_scripts_install(so_module *);
extern int acp_vita_scripts_prepare(lua_State *);
extern void acp_log(const char *,...);
#ifdef ACP_DEVELOPMENT_PROFILE
extern int acp_method_profiling(void);
#endif
extern int acp_jit_init(void);
extern void acp_jit_clear_cache(void *,void *);
static int (*gc_loop)(void *,void *);
static int (*gc_step_original)(void *);
static acp_lua_heap *gc_heap;
static uintptr_t async_global;
static uint32_t async_seen;
static unsigned gc_deferred,gc_steps,gc_samples,gc_sample_us,gc_worker_us,gc_cycles,gc_emergencies;
static uint32_t gc_emergency_at;
void acp_gc_window_seen(void *global){
    __atomic_store_n(&async_seen,(uint32_t)sceKernelGetProcessTimeWide(),__ATOMIC_RELEASE);
    __atomic_store_n(&async_global,(uintptr_t)global,__ATOMIC_RELEASE);
}
/* Allocator callback near the arena limit, with the heap lock held. When a
 * cycle ends LuaJIT sets its threshold to estimate*pause, beyond a 96 MiB
 * arena once ~50 MiB is live, and the worker only runs between frames: a
 * long loading frame then allocates until the arena is exhausted. Lowering
 * the threshold to the current total only makes the next lj_gc_check step;
 * LuaJIT stores the same value itself while a cycle is in debt. */
static void gc_pressure(void *ctx){
    (void)ctx;uint8_t *g=(uint8_t *)__atomic_load_n(&async_global,__ATOMIC_ACQUIRE);
    if(g&&*(uint32_t *)(g+0x18)>*(uint32_t *)(g+0x14))*(uint32_t *)(g+0x18)=*(uint32_t *)(g+0x14);
}
static int foreground_gc(void *state){
    uint8_t *g=*(uint8_t **)((uint8_t *)state+8);
    gc_heap->ops.lock(gc_heap->ops.ctx);
    size_t used=gc_heap->used,limit=gc_heap->limit;
    gc_heap->ops.unlock(gc_heap->ops.ctx);
    if(used<=limit&&limit-used<=ACP_GC_EMERGENCY_HEADROOM){
        uint32_t now=(uint32_t)sceKernelGetProcessTimeWide();
        if(!gc_emergency_at||now-gc_emergency_at>=ACP_GC_EMERGENCY_INTERVAL_US){
            /* lj_gc_step: 0 = still in debt, -1 = step paid, 1 = cycle done.
             * Repeating it is what LuaJIT's JIT exit path does; it only
             * finishes the current cycle sooner, at a verified safe point. */
            gc_emergency_at=now|1u;__atomic_fetch_add(&gc_emergencies,1,__ATOMIC_RELAXED);
            int result;unsigned steps=0;
            do result=gc_step_original(state);while((result==0||result==-1)&&++steps<(1u<<20));
            return result;
        }
    }
    if((uintptr_t)g==__atomic_load_n(&async_global,__ATOMIC_ACQUIRE)){
        uint32_t age=(uint32_t)sceKernelGetProcessTimeWide()-__atomic_load_n(&async_seen,__ATOMIC_ACQUIRE);
        if(age<=200000u && *(uint32_t *)(g+0x14)<64u*1024u*1024u){
            uint32_t threshold=acp_gc_defer_threshold(*(uint32_t *)(g+0x14),
                *(uint32_t *)(g+0x44),*(uint32_t *)(g+0x48),age,used,limit);
            if(threshold){
                *(uint32_t *)(g+0x18)=threshold;
                __atomic_fetch_add(&gc_deferred,1,__ATOMIC_RELAXED);return -1;
            }
        }
    }
    unsigned n=__atomic_fetch_add(&gc_steps,1,__ATOMIC_RELAXED);
    /* Sample 1/64 fallback steps, so timing does not add two system calls to
     * every 1 KiB allocation during startup or a high-pressure collection. */
    if(n%64u)return gc_step_original(state);
    uint32_t begin=(uint32_t)sceKernelGetProcessTimeWide();
    int result=gc_step_original(state);
    __atomic_fetch_add(&gc_sample_us,(uint32_t)sceKernelGetProcessTimeWide()-begin,__ATOMIC_RELAXED);
    __atomic_fetch_add(&gc_samples,1,__ATOMIC_RELAXED);return result;
}
void acp_gc_report(void){
    extern void acp_gc_window_report(void);acp_gc_window_report();
#define TAKE(n) __atomic_exchange_n(&(n),0,__ATOMIC_RELAXED)
    unsigned deferred=TAKE(gc_deferred),steps=TAKE(gc_steps),samples=TAKE(gc_samples),time=TAKE(gc_sample_us);
    size_t live=0,cached=0;unsigned reused=0,backend=0,evicted=0;
    if(gc_heap){
        gc_heap->ops.lock(gc_heap->ops.ctx);
        live=gc_heap->used;cached=gc_heap->cached;
        reused=gc_heap->cache_hits;backend=gc_heap->backend_calls;
        gc_heap->cache_hits=gc_heap->backend_calls=0;
        evicted=gc_heap->cache_evicted;gc_heap->cache_evicted=0;
#ifdef ACP_DEVELOPMENT_PROFILE
        if(acp_method_profiling()){
            for(unsigned n=0;n<5;n++){
                unsigned best=0;
                for(unsigned i=1;i<ACP_LUA_SMALL_CLASSES;i++)if(gc_heap->cache_misses[i]>gc_heap->cache_misses[best])best=i;
                if(!gc_heap->cache_misses[best])break;
                acp_log("Lua cache miss: size=%u count=%u cached-blocks=%u",(best+1)*16,gc_heap->cache_misses[best],gc_heap->cache_counts[best]);
                gc_heap->cache_misses[best]=0;
            }
            for(unsigned i=0;i<ACP_LUA_SMALL_CLASSES;i++)if(gc_heap->cache_counts[i]*(i+1)*16>128*1024)
                acp_log("Lua cache holding: size=%u bytes=%u",(i+1)*16,gc_heap->cache_counts[i]*(i+1)*16);
            memset(gc_heap->cache_misses,0,sizeof(gc_heap->cache_misses));
        }
#endif
        gc_heap->ops.unlock(gc_heap->ops.ctx);
    }
    acp_log("Lua GC window: deferred=%u foreground=%u sampled=%u sample-total=%u us worker=%u us cycles=%u emergency=%u live=%u KiB cached=%u KiB reuse=%u backend=%u",
        deferred,steps,samples,time,TAKE(gc_worker_us),TAKE(gc_cycles),TAKE(gc_emergencies),(unsigned)(live/1024),(unsigned)(cached/1024),reused,backend);
    acp_lua_backend_stats b;
    acp_log("Lua cache replacement: recycled-cold-blocks=%u",evicted);
    if(!acp_lua_vita_sample(gc_heap,&b)){
        acp_log("Lua backend TLSF (1/256): resize=%u calls sampled=%u us n=%u free=%u calls sampled=%u us n=%u",
            b.resize_calls,(unsigned)b.resize_us,b.resize_samples,b.free_calls,(unsigned)b.free_us,b.free_samples);
        acp_log("Lua heap lock: atomic-fast=%u semaphore-wait=%u",b.lock_fast,b.lock_wait);
    }
#undef TAKE
}
static int paced_gc(void *state,void *running){
    extern void acp_gc_thread_record(void);acp_gc_thread_record();
    uint8_t *g=*(uint8_t **)((uint8_t *)state+8);
    uint32_t begin=(uint32_t)sceKernelGetProcessTimeWide();
    __atomic_store_n(&async_seen,begin,__ATOMIC_RELEASE);
    __atomic_store_n(&async_global,(uintptr_t)g,__ATOMIC_RELEASE);
    uint32_t total=*(uint32_t *)(g+0x14);
    if(!acp_gc_cycle_due(g[0x1d],total,*(uint32_t *)(g+0x44),*(uint32_t *)(g+0x48))){
        sceKernelDelayThread(100);return 0;
    }
    *(uint32_t *)(g+0x18)=total>=1024?total-1024:0;
    int result=gc_loop(state,running);
    __atomic_fetch_add(&gc_worker_us,(uint32_t)sceKernelGetProcessTimeWide()-begin,__ATOMIC_RELAXED);
    if(result==1)__atomic_fetch_add(&gc_cycles,1,__ATOMIC_RELAXED);
    return result;
}
static void idle(int64_t us){
    while(us>0){unsigned part=(unsigned)(us>1000000?1000000:us);if(sceKernelDelayThread(part)<0)return;us-=part;}
}
int acp_game_patch_vita(so_module *m,acp_lua_heap *lua_heap){
    if(!m||!lua_heap||acp_game_patch_validate((const uint8_t*)m->load_addr,m->text_size))return -1;
    acp_lua_api api={
        (void*)so_symbol(m,"lua_newstate"),(void*)so_symbol(m,"lua_atpanic"),
        (void*)so_symbol(m,"luaL_openlibs"),(void*)so_symbol(m,"luaJIT_setmode"),
        (void*)so_symbol(m,"lua_close"),(void*)so_symbol(m,"lua_cpcall"),
        (void*)(m->load_addr+0x529d79u),acp_vita_scripts_prepare};
    if(acp_vita_scripts_install(m))return -1;
    if(acp_lua_install(lua_heap,&api))return -1;
    /* The ARM Android cache-flush helper contains a Linux SVC; route it to
     * kubridge before any JIT-generated code can execute on Vita. */
    const uint8_t clear_guard[10]={0x80,0xb4,0x40,0xf2,0x02,0x07,0xc0,0xf2,0x0f,0x07};
    uintptr_t clear=so_symbol(m,"__clear_cache");uint8_t clear_jump[8];
    if(!clear||memcmp((void *)(clear&~1u),clear_guard,sizeof(clear_guard))||
       acp_game_patch_build((uint32_t)(uintptr_t)acp_jit_clear_cache,clear_jump))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)(clear&~1u),clear_jump,sizeof(clear_jump))<0)return -1;
    acp_lua_enable_jit(acp_jit_init());
    uint8_t lua_jump[8],idle_jump[8],gc_jump[8],step_jump[8],trampoline[24];
    if(acp_game_patch_build((uint32_t)(uintptr_t)acp_lua_create_state,lua_jump)||
       acp_game_patch_build((uint32_t)(uintptr_t)idle,idle_jump)||
       acp_game_patch_build((uint32_t)(uintptr_t)paced_gc,gc_jump)||
       acp_game_patch_build((uint32_t)(uintptr_t)foreground_gc,step_jump)||
       acp_gc_trampoline_build((uint32_t)(m->load_addr+ACP_GC_STEP_VA+14u)|1u,trampoline))return -1;
    gc_loop=(void *)(m->load_addr+0x6bf4d1u);
    uintptr_t entry=(m->patch_head+3u)&~(uintptr_t)3u;
    if(entry<m->patch_base||entry-m->patch_base>m->patch_size||
       sizeof(trampoline)>m->patch_size-(entry-m->patch_base))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)entry,trampoline,sizeof(trampoline))<0)return -1;
    /* so_flush_caches only covers the ELF text, not so_util's patch arena. */
    kuKernelFlushCaches((void *)entry,sizeof(trampoline));
    m->patch_head=entry+sizeof(trampoline);gc_step_original=(void *)(entry|1u);gc_heap=lua_heap;
    lua_heap->ops.lock(lua_heap->ops.ctx);
    lua_heap->pressure=gc_pressure;lua_heap->pressure_ctx=NULL;lua_heap->pressure_headroom=ACP_GC_PRESSURE_HEADROOM;
    lua_heap->ops.unlock(lua_heap->ops.ctx);
    /* Any partial native write failure is a fatal boot failure. The caller
     * must not enter constructors after a nonzero result. */
    if(kuKernelCpuUnrestrictedMemcpy((void*)(m->load_addr+ACP_ALLOC_LUA_VA),lua_jump,sizeof(lua_jump))<0)return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void*)(m->load_addr+ACP_IDLE_VA),idle_jump,sizeof(idle_jump))<0)return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void*)(m->load_addr+ACP_GC_VA),gc_jump,sizeof(gc_jump))<0)return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void*)(m->load_addr+ACP_GC_STEP_VA),step_jump,sizeof(step_jump))<0)return -1;
    acp_log("Lua GC: early worker cycles, bounded small-block reuse; original collector fallback at 64 MiB / 24 MiB arena headroom / 200 ms worker timeout; forced steps within 24 MiB and cycle completion within 8 MiB of the arena limit");
    /* DevicePublicGameDataFolder appends '/' and a NUL to the directory.
     * The original allocates strlen+1, overrunning by one byte. */
    const uint8_t directory_size[2]={0x02,0x30}; /* adds r0,#2 */
    if(kuKernelCpuUnrestrictedMemcpy((void*)(m->load_addr+ACP_DIRECTORY_SIZE_VA),directory_size,sizeof(directory_size))<0)return -1;
    extern int acp_plain_assets_install(so_module *);
    if(acp_plain_assets_install(m))return -1;
    so_flush_caches(m);return 0;
}
