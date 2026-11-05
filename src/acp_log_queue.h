#ifndef ACP_LOG_QUEUE_H
#define ACP_LOG_QUEUE_H
#include <stddef.h>
#define ACP_LOG_QUEUE_BYTES 65536u
/* Caller serializes operations. Full queues drop complete records instead
 * of blocking the rendering thread on storage. */
typedef struct {char data[ACP_LOG_QUEUE_BYTES];size_t head,used;unsigned dropped;} acp_log_queue;
int acp_log_queue_push(acp_log_queue *,const char *,size_t);
size_t acp_log_queue_pop(acp_log_queue *,char *,size_t);
#endif
