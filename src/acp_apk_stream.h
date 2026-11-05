#ifndef ACP_APK_STREAM_H
#define ACP_APK_STREAM_H
#include <stddef.h>
#include <stdint.h>
typedef struct {const char *name;uint32_t offset,size;int stored;} acp_apk_entry;
typedef struct {const acp_apk_entry *entry;uint32_t position;} acp_apk_cursor;
const acp_apk_entry *acp_apk_find(const acp_apk_entry *,size_t,const char *);
int acp_apk_read(acp_apk_cursor *,void *,unsigned,int (*)(void *,void *,unsigned,uint32_t),void *);
int64_t acp_apk_seek(acp_apk_cursor *,int64_t);
#endif
