#ifndef ACP_ASSETS_H
#define ACP_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#define ACP_ASSET_HANDLES 64
typedef struct {
    void *ctx;
    void (*lock)(void *),(*unlock)(void *);
    int (*open)(void *,const char *,uint64_t *);
    int (*read)(void *,int,void *,unsigned);
    int64_t (*seek)(void *,int,int64_t);
    int (*close)(void *,int);
} acp_asset_io;
typedef struct {int fd,active;uint32_t size,position;} acp_asset;
typedef struct {
    acp_asset_io io;
    char root[256];
    acp_asset handles[ACP_ASSET_HANDLES];
} acp_assets;
int acp_assets_init(acp_assets *,const char *,const acp_asset_io *);
acp_asset *acp_asset_open(acp_assets *,const char *);
int acp_asset_read(acp_assets *,acp_asset *,void *,size_t);
int32_t acp_asset_seek(acp_assets *,acp_asset *,int32_t,int);
int32_t acp_asset_length(acp_assets *,acp_asset *,int remaining);
int acp_asset_close(acp_assets *,acp_asset *);
int acp_assets_idle(acp_assets *);
/* Native bridge for the game's AAsset imports; preserves the Android 32-bit
 * off_t ABI. Storage remains file-backed. No AAsset_getBuffer implementation. */
int acp_assets_vita_init(void);
void *acp_assets_vita_manager(void);
uintptr_t acp_assets_resolve(const char *);
#endif
