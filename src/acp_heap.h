/* Bounded aligned allocation accounting, independent of platform size queries. MIT. */
#ifndef ACP_HEAP_H
#define ACP_HEAP_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    void *context;
    void *(*resize)(void *,void *,size_t);
    void (*release)(void *,void *);
    size_t limit,used,peak,blocks,failures;
} acp_heap;
/* Caller serializes all operations. Backing allocator enforces physical capacity. */
void *acp_heap_alloc(acp_heap *,size_t alignment,size_t bytes);
void *acp_heap_resize(acp_heap *,void *,size_t bytes);
int acp_heap_free(acp_heap *,void *);
size_t acp_heap_size(acp_heap *,void *);
#endif
