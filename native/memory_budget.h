#ifndef ACP_MEMORY_BUDGET_H
#define ACP_MEMORY_BUDGET_H
/* 00.35 hardware had only 58 MiB free before graphics, below its 64 MiB
 * pool-plus-reserve requirement. Release 16 MiB from the game reservation;
 * measured opening-scene game peaks are 100-102 MiB. Keep Lua at 96 MiB:
 * its prior 64 MiB arena exhausted during scene loading. The game heap spills
 * over only as below (00.75); the Lua arena may add 8 MiB blocks after VitaGL is up, only
 * when a request still fails and main RAM stays above a 16 MiB floor
 * (acp_lua_vita.h). Not part of this start-up budget. */
#define ACP_GAME_HEAP_MIB 128u
/* Only when a request still fails, the game heap may add up to 4 pools of
 * at least 8 MiB, never leaving less than 16 MiB of main RAM (memory.c). */
#define ACP_GAME_HEAP_EXTENSIONS 4u
#define ACP_GAME_HEAP_EXTENSION_BYTES (8u*1024u*1024u)
#define ACP_GAME_HEAP_FREE_FLOOR (16u*1024u*1024u)
#define ACP_LUA_HEAP_MIB 96u
#define ACP_GAME_HEAP_BYTES (ACP_GAME_HEAP_MIB*1024u*1024u)
#define ACP_LUA_HEAP_BYTES (ACP_LUA_HEAP_MIB*1024u*1024u)
#endif
