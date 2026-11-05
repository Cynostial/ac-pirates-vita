#ifndef ACP_SHADER_CACHE_H
#define ACP_SHADER_CACHE_H
#include <stddef.h>
#define ACP_SHADER_CACHE_MAX (256u*1024u)
void acp_shader_cache_key(const void *source,size_t source_size,const void *settings,size_t settings_size,char key[65]);
/* Returned payload is owned by the caller (free). Invalid caches are misses. */
size_t acp_shader_cache_read(const char *directory,const char *key,void **payload);
int acp_shader_cache_write(const char *directory,const char *key,const void *payload,size_t size);
#endif
