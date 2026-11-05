#ifndef ACP_STARTUP_CACHE_H
#define ACP_STARTUP_CACHE_H
#include <stdio.h>
typedef struct acp_cache_file acp_cache_file;
void acp_startup_cache_init(void);
/* Takes ownership of backing on success; otherwise leaves it unchanged. */
FILE *acp_startup_cache_file(FILE *backing,acp_cache_file **cookie);
/* Convert to the real file before exposing its descriptor. */
FILE *acp_startup_cache_detach(FILE *filtered,acp_cache_file *cookie);
#endif
