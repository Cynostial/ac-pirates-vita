#include "acp_frame_wait.h"
#include <limits.h>
static void until(const acp_wait_ops *ops,void *ctx,uint64_t deadline){
    uint64_t now;
    while((now=ops->now(ctx))<deadline){
        uint64_t remaining=deadline-now;
        ops->sleep(ctx,(unsigned)(remaining>1000000u?1000000u:remaining));
    }
}
void acp_wait_run(acp_wait_state *s,const acp_wait_ops *ops,void *ctx,unsigned us,int gc){
    if(!us)return;
    uint64_t begin=ops->now(ctx),deadline=begin+us;
    if(gc){
        unsigned guard=s->allowance?s->allowance:1000u;
        if(guard>us)guard=us;
        ops->start_gc(ctx);
        until(ops,ctx,deadline-guard);
        uint64_t stop_begin=ops->now(ctx);
        ops->stop_gc(ctx);
        uint64_t cost=ops->now(ctx)-stop_begin;
        unsigned measured=(unsigned)(cost>UINT_MAX?UINT_MAX:cost);
        s->stop_time+=cost;s->collections++;
        if(measured>s->max_stop)s->max_stop=measured;
        unsigned desired=measured>=7750u?8000u:measured+250u;
        /* Retain allowance for a slow join across several seconds of frames;
         * a few fast joins must not immediately expose the same late spike. */
        unsigned decayed=s->allowance-s->allowance/128u;
        s->allowance=desired>decayed?desired:decayed;
        if(s->allowance<250u)s->allowance=250u;
        if(s->allowance>8000u)s->allowance=8000u;
    }
    until(ops,ctx,deadline);
    uint64_t elapsed=ops->now(ctx)-begin;
    s->requests++;s->requested+=us;s->elapsed+=elapsed;
    uint64_t over=elapsed>us?elapsed-us:0;
    unsigned excess=(unsigned)(over>UINT_MAX?UINT_MAX:over);
    if(excess>s->max_overrun)s->max_overrun=excess;
}
