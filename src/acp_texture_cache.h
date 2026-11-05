#ifndef ACP_TEXTURE_CACHE_H
#define ACP_TEXTURE_CACHE_H
#include <stddef.h>
#define ACP_TEXTURE_CACHE_MAX (8u*1024u*1024u)
void acp_texture_cache_key(const void *source,size_t bytes,unsigned width,unsigned height,char out[65]);
/* Read directly into the caller's existing GPU allocation. A failed read may
 * overwrite it; the caller must run the original converter on a miss. */
int acp_texture_cache_read(const char *directory,const char *key,void *out,size_t bytes);
int acp_texture_cache_write(const char *directory,const char *key,const void *data,size_t bytes);
#endif
