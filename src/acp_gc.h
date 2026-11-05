#ifndef ACP_GC_H
#define ACP_GC_H
#include <stdint.h>
#include <stddef.h>
/* Within ACP_GC_PRESSURE_HEADROOM of the Lua arena's limit every allocation
 * makes the next safe point run a foreground step, even when LuaJIT's own
 * threshold (estimate*pause) lies beyond the arena. Within the emergency
 * headroom a foreground step completes the cycle, at most once per interval. */
#define ACP_GC_PRESSURE_HEADROOM (24u*1024u*1024u)
#define ACP_GC_EMERGENCY_HEADROOM (8u*1024u*1024u)
#define ACP_GC_EMERGENCY_INTERVAL_US 2000000u
/* Start a background cycle before reaching foreground memory pressure. */
int acp_gc_cycle_due(unsigned state,uint32_t total,uint32_t estimate,uint32_t pause);
/* Zero means run the original foreground step. A nonzero return is the next
 * allocation threshold; neither the collector nor its write barriers change. */
uint32_t acp_gc_defer_threshold(uint32_t total,uint32_t estimate,uint32_t pause,
                              uint32_t worker_age_us,size_t arena_used,size_t arena_limit);
#endif
