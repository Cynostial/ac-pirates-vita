#ifndef ACP_LUA_CACHE_H
#define ACP_LUA_CACHE_H
#include <stddef.h>
#include <stdint.h>
/* Persistent bytecode for the game's own Lua chunks. Entries are keyed by the
 * chunk name and the exact source bytes and were produced by the same VM.
 * Records are checksummed; a truncated or damaged tail is discarded. The
 * stored file is read once into one caller-provided allocation. */
typedef struct {
    void *(*allocate)(size_t);
    void (*release)(void *);
} acp_lua_cache_memory;
typedef struct {uint32_t hits,misses,stored,rejected,loaded_bytes,file_bytes;} acp_lua_cache_stats;
/* Open path for this build tag (8 bytes). Never fails: an unusable file
 * starts an empty cache. limit bounds the file size. */
void acp_lua_cache_open(const char *path,const char tag[8],uint32_t limit,const acp_lua_cache_memory *memory);
void acp_lua_cache_close(void);
uint64_t acp_lua_cache_key(const char *name,const void *source,size_t size);
/* Returns stored bytecode for key and exact source size, or NULL. */
const void *acp_lua_cache_find(uint64_t key,uint32_t source_size,uint32_t *code_size);
/* Append bytecode produced from the exact source; ignored when full/failed. */
void acp_lua_cache_store(uint64_t key,uint32_t source_size,const void *code,uint32_t code_size);
/* A found entry failed to load; do not offer it again this session. */
void acp_lua_cache_reject(uint64_t key,uint32_t source_size);
acp_lua_cache_stats acp_lua_cache_stats_get(void);
#endif
