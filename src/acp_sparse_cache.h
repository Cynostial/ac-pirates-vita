#ifndef ACP_SPARSE_CACHE_H
#define ACP_SPARSE_CACHE_H
#include <stddef.h>
#include <stdint.h>
#define ACP_CACHE_LIMIT (6u*1024u*1024u)
typedef struct {
    const unsigned char *bytes;
    uint32_t count, source_bytes;
} acp_sparse_cache;
/* The caller owns immutable storage for the lifetime of the view. */
int acp_sparse_cache_init(acp_sparse_cache *,const void *,size_t,uint32_t);
/* Return a contiguous cached span or an uncached gap (data == NULL).
 * This never reads past the archive end and requires no allocation. */
size_t acp_sparse_cache_span(const acp_sparse_cache *,uint32_t,size_t,const unsigned char **);
#endif
