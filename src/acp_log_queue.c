#include "acp_log_queue.h"
#include <string.h>
int acp_log_queue_push(acp_log_queue *q,const char *s,size_t n){
    if(n>ACP_LOG_QUEUE_BYTES-q->used){q->dropped++;return -1;}
    size_t tail=(q->head+q->used)%ACP_LOG_QUEUE_BYTES;
    size_t first=n<ACP_LOG_QUEUE_BYTES-tail?n:ACP_LOG_QUEUE_BYTES-tail;
    memcpy(q->data+tail,s,first);memcpy(q->data,s+first,n-first);q->used+=n;return 0;
}
size_t acp_log_queue_pop(acp_log_queue *q,char *s,size_t n){
    if(n>q->used)n=q->used;
    size_t first=n<ACP_LOG_QUEUE_BYTES-q->head?n:ACP_LOG_QUEUE_BYTES-q->head;
    memcpy(s,q->data+q->head,first);memcpy(s+first,q->data,n-first);
    q->head=(q->head+n)%ACP_LOG_QUEUE_BYTES;q->used-=n;return n;
}
