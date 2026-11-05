#ifndef ACP_GAME_PATCH_H
#define ACP_GAME_PATCH_H
#include <stddef.h>
#include <stdint.h>
#include "acp_lua.h"
/* These byte guards are for SHA256 1dc7f264...cc291a7 only. All guards must
 * match before any write. The loader must additionally verify the full file. */
#define ACP_ALLOC_LUA_VA 0x529fb8u
#define ACP_IDLE_VA 0x52ab2cu
#define ACP_GC_VA 0x6bc308u
#define ACP_GC_STEP_VA 0x6bf3e8u
#define ACP_DIRECTORY_SIZE_VA 0x9176e6u
int acp_game_patch_validate(const uint8_t *mapped,size_t bytes);
int acp_game_patch_build(uint32_t destination,uint8_t out[8]);
/* 14-byte, position-independent prologue; padding keeps the jump aligned. */
int acp_gc_trampoline_build(uint32_t continuation,uint8_t out[24]);
void acp_gc_report(void);
struct so_module;
int acp_game_patch_vita(struct so_module *,acp_lua_heap *);
#endif
