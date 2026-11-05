#ifndef ACP_LUA_H
#define ACP_LUA_H
#include <stddef.h>
#include <stdint.h>
typedef struct lua_State lua_State;
typedef void *(*acp_lua_alloc_fn)(void *,void *,size_t,size_t);
typedef int (*acp_lua_cfunction)(lua_State *);
typedef struct {
    void *ctx;void (*lock)(void *),(*unlock)(void *);
    /* realloc semantics: failure preserves the original allocation. The
     * backend is a fixed arena; it only gains backing through heap.grow. */
    void *(*resize)(void *,void *,size_t);void (*free)(void *,void *);
} acp_lua_heap_ops;
#define ACP_LUA_SMALL_MAX 4096u
#define ACP_LUA_SMALL_CLASSES (ACP_LUA_SMALL_MAX/16u)
typedef struct {
    acp_lua_heap_ops ops;size_t limit,used,peak,blocks;unsigned failures;
    /* Freed small blocks remain in the same fixed arena, up to 2 MiB.
     * No extra per-object header: the size class follows from info.bytes. */
    void *small[ACP_LUA_SMALL_CLASSES];size_t cached,cache_limit;unsigned cache_hits,backend_calls;
    /* Second-chance replacement is per size class, not per object. It only
     * releases already-free blocks and never grows the existing cache cap. */
    unsigned char cache_recent[ACP_LUA_SMALL_CLASSES];unsigned cache_hand,cache_evicted;
    unsigned cache_counts[ACP_LUA_SMALL_CLASSES];
    /* Optional, called with the lock held. grow: a request did not fit even
     * after trimming; returns bytes of new backing added to the arena (0 =
     * none) and the limit rises by that much. pressure: an allocation left
     * pressure_headroom bytes or less below the limit. */
    size_t (*grow)(void *ctx,size_t bytes);
    void (*pressure)(void *ctx);void *pressure_ctx;
    size_t pressure_headroom,grown,last_failure;
#ifdef ACP_DEVELOPMENT_PROFILE
    unsigned cache_misses[ACP_LUA_SMALL_CLASSES];
#endif
} acp_lua_heap;
int acp_lua_heap_init(acp_lua_heap *,size_t,const acp_lua_heap_ops *);
void *acp_lua_allocate(void *,void *,size_t old_size,size_t new_size);
typedef struct {
    lua_State *(*newstate)(acp_lua_alloc_fn,void *);
    acp_lua_cfunction (*atpanic)(lua_State *,acp_lua_cfunction);
    void (*openlibs)(lua_State *);int (*setmode)(lua_State *,int,int);
    void (*close)(lua_State *);int (*cpcall)(lua_State *,acp_lua_cfunction,void *);
    acp_lua_cfunction panic;
    int (*prepare)(lua_State *);
} acp_lua_api;
/* Called once before constructors. Replacement for the game's AllocLuaState:
 * fresh bounded allocator, original panic callback, protected library opening,
 * and JIT disabled during library setup. Native compilation is opt-in only
 * after the platform has reserved bounded executable code memory. */
int acp_lua_install(acp_lua_heap *,const acp_lua_api *);
void acp_lua_enable_jit(int enabled);
lua_State *acp_lua_create_state(void);
#endif
