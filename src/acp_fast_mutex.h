#ifndef ACP_FAST_MUTEX_H
#define ACP_FAST_MUTEX_H
#include <stdint.h>
/* Nonrecursive benaphore. Owners + waiters remain counted until unlock;
 * contended callers sleep instead of spinning on the render/GC thread.
 * Create/delete and the callbacks require a quiescent lifetime boundary. */
typedef struct {uint32_t users;} acp_fast_mutex;
typedef int (*acp_fast_mutex_op)(void *);
int acp_fast_mutex_lock(acp_fast_mutex *,acp_fast_mutex_op wait,void *context);
int acp_fast_mutex_unlock(acp_fast_mutex *,acp_fast_mutex_op signal,void *context);
int acp_fast_mutex_idle(const acp_fast_mutex *);
#endif
