#ifndef ACP_CACHE_FILE_H
#define ACP_CACHE_FILE_H
#include <stdio.h>
#include <stdint.h>
/* The pinned OBB is a single-disk, stored ZIP with no comment or ZIP64.
 * Extract one member using 8 KiB of working storage; never load the archive
 * or the complete member. Caller serializes copies and owns the input FILE.
 * Return 0 copied, 1 reused by length (Android behavior), -1 on failure. */
int acp_copy_cache_member(FILE *,const char *cache_dir,const char *member,const char *name,char *out,size_t capacity);
/* Optional central-directory offset. The header, exact path and local header
 * are still validated. A stale hint falls back to the normal directory scan. */
int acp_copy_cache_member_hint(FILE *,const char *,const char *,const char *,char *,size_t,uint32_t);
#endif
