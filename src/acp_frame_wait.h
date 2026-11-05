#ifndef ACP_FRAME_WAIT_H
#define ACP_FRAME_WAIT_H
#include <stdint.h>
typedef struct {
    uint64_t (*now)(void *);
    void (*sleep)(void *,unsigned);
    void (*start_gc)(void *);
    void (*stop_gc)(void *);
} acp_wait_ops;
typedef struct {
    unsigned allowance,requests,collections,max_stop,max_overrun;
    uint64_t requested,elapsed,stop_time;
} acp_wait_state;
/* Finish joining the existing GC worker before the requested deadline.
 * Joining is never skipped: Lua cannot resume while that worker is active. */
void acp_wait_run(acp_wait_state *,const acp_wait_ops *,void *,unsigned,int);
#endif
