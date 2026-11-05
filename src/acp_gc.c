#include "acp_gc.h"
/* Pacing policy adapted from KLM-corporation/ac-pirates-arm64 (MIT, 2026).
 * Keep the game's original collector and all write barriers. Only avoid
 * starting another full cycle while idle and below the allocation threshold. */
int acp_gc_cycle_due(unsigned state,uint32_t total,uint32_t estimate,uint32_t pause){
    uint64_t trigger=(uint64_t)(estimate/100u)*pause;
    /* Begin in the worker before the 64 MiB foreground pressure boundary.
     * Leave at least 2 MiB above the previous live set so a large live heap
     * does not start another cycle immediately after finishing one. */
    uint64_t early=(uint64_t)estimate+estimate/4u;
    if(early>48u*1024u*1024u)early=48u*1024u*1024u;
    if(early<(uint64_t)estimate+2u*1024u*1024u)early=(uint64_t)estimate+2u*1024u*1024u;
    if(early>56u*1024u*1024u)early=56u*1024u*1024u;
    if(trigger>early)trigger=early;
    return state!=0 || total>=trigger;
}
uint32_t acp_gc_defer_threshold(uint32_t total,uint32_t estimate,uint32_t pause,
                              uint32_t worker_age_us,size_t arena_used,size_t arena_limit){
    const uint32_t cap=64u*1024u*1024u,quantum=256u*1024u;
    /* The stock Vita keeps much less headroom than the reference Android
     * wrapper: stop deferring at 64 MiB of Lua objects or within 24 MiB of the
     * fixed arena's limit, including allocator headers and other Lua states. */
    if(worker_age_us>200000u||arena_used>arena_limit||
       arena_limit-arena_used<=24u*1024u*1024u)return 0;
    uint64_t limit=(uint64_t)(estimate/100u)*pause*3u/2u;
    if(limit>cap)limit=cap;
    if(total>=limit)return 0;
    return (uint32_t)(limit-total<quantum?limit:total+quantum);
}
