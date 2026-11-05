#include "acp_fast_mutex.h"
#include <limits.h>
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t),0),"32-bit lock-free atomics required");
int acp_fast_mutex_lock(acp_fast_mutex *m,acp_fast_mutex_op wait,void *context){
    uint32_t previous=__atomic_fetch_add(&m->users,1,__ATOMIC_ACQUIRE);
    if(previous>=INT_MAX)return -1; /* Fatal to caller; never continue corrupted. */
    if(previous){
        if(wait(context)<0)return -1;
        /* The semaphore handoff publishes the previous owner's writes. */
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        return 1;
    }
    return 0;
}
int acp_fast_mutex_unlock(acp_fast_mutex *m,acp_fast_mutex_op signal,void *context){
    uint32_t previous=__atomic_fetch_sub(&m->users,1,__ATOMIC_RELEASE);
    if(!previous)return -1;
    return previous>1?signal(context):0;
}
int acp_fast_mutex_idle(const acp_fast_mutex *m){return __atomic_load_n(&m->users,__ATOMIC_ACQUIRE)==0;}
