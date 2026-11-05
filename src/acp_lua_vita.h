#ifndef ACP_LUA_VITA_H
#define ACP_LUA_VITA_H
#include "acp_lua.h"
#include "acp_fast_mutex.h"
#include <psp2/kernel/threadmgr.h>
typedef struct {unsigned resize_calls,free_calls,resize_samples,free_samples;uint64_t resize_us,free_us;unsigned lock_fast,lock_wait;} acp_lua_backend_stats;
#define ACP_LUA_VITA_EXTENSIONS 4u
#define ACP_LUA_VITA_EXTENSION_BYTES (8u*1024u*1024u)
/* Extensions never leave less than this much main RAM free for the system. */
#define ACP_LUA_VITA_FREE_FLOOR (16u*1024u*1024u)
typedef struct {
    int block,mutex_ready,semaphore;acp_fast_mutex mutex;void *arena,*pool;size_t backing;acp_lua_heap heap;
    unsigned resize_tick,free_tick;acp_lua_backend_stats stats;
    int extension[ACP_LUA_VITA_EXTENSIONS];unsigned extensions;
} acp_lua_vita;
/* Admission is this native backing, including allocator metadata and
 * fragmentation. Caller accounts it once in the application main-RAM budget.
 * Only when a request still fails, up to ACP_LUA_VITA_EXTENSIONS blocks of
 * 8 MiB are added while main RAM stays above ACP_LUA_VITA_FREE_FLOOR. */
int acp_lua_vita_create(acp_lua_vita *,size_t backing);
/* Only after all Lua states are closed and engine callers are quiescent. */
int acp_lua_vita_destroy(acp_lua_vita *);
/* Snapshot/reset sampled backend timings. No per-allocation logging. */
int acp_lua_vita_sample(acp_lua_heap *,acp_lua_backend_stats *);
/* Lends free arena space to a big transient game-heap block, charged to the
 * Lua heap until reclaimed; NULL unless ACP_LUA_VITA_LEND_RESERVE stays free
 * below the limit afterwards. Largest free block: diagnostics (walks pools). */
#define ACP_LUA_VITA_LEND_RESERVE (16u*1024u*1024u)
void *acp_lua_vita_lend(size_t alignment,size_t bytes);
void acp_lua_vita_reclaim(void *);
size_t acp_lua_vita_largest_free(void);
#endif
