/* One read-only APK handle, independent bounded cursors, no asset-size RAM cache. */
#include "acp_native.h"
#include "acp_apk_stream.h"
#include "acp_assets.h"
#include "acp_sha256.h"
#include <psp2/io/fcntl.h>
#include <string.h>
#include "apk_index.inc"
static int apk_fd=-1;
static acp_apk_cursor cursors[ACP_ASSET_HANDLES];
int acp_apk_stream_init(void){
    int fd=sceIoOpen(ACP_DATA_PATH "/game.apk",SCE_O_RDONLY,0);if(fd<0)return 0;
    unsigned char data[8192],digest[32];acp_sha256 sha;acp_sha256_init(&sha);
    if(sceIoLseek(fd,0,SCE_SEEK_END)!=ACP_APK_SIZE||sceIoLseek(fd,ACP_APK_DIRECTORY,SCE_SEEK_SET)<0)goto invalid;
    unsigned left=ACP_APK_SIZE-ACP_APK_DIRECTORY;
    while(left){unsigned chunk=left>sizeof(data)?sizeof(data):left;int n=sceIoRead(fd,data,chunk);if(n<=0||(unsigned)n>chunk)goto invalid;acp_sha256_update(&sha,data,n);left-=n;}
    acp_sha256_final(&sha,digest);if(memcmp(digest,apk_directory_hash,32))goto invalid;
    apk_fd=fd;acp_log("APK assets: verified directory, one shared archive handle, %u indexed names",(unsigned)(sizeof(apk_entries)/sizeof(*apk_entries)));return 1;
invalid:
    sceIoClose(fd);acp_log("APK index unavailable; retaining loose asset files");return 0;
}
/* -2 means the caller should use its existing loose-file path. */
int acp_apk_stream_open(const char *path,uint64_t *size){
    const char prefix[]=ACP_DATA_PATH "/assets/";
    if(apk_fd<0||strncmp(path,prefix,sizeof(prefix)-1))return -2;
    const acp_apk_entry *e=acp_apk_find(apk_entries,sizeof(apk_entries)/sizeof(*apk_entries),path+sizeof(prefix)-1);
    if(!e)return -1;
    if(!e->stored)return -2;
    for(unsigned i=0;i<ACP_ASSET_HANDLES;i++)if(!cursors[i].entry){cursors[i]=(acp_apk_cursor){e,0};*size=e->size;return (int)i;}
    return -1;
}
static int read_at(void *ctx,void *out,unsigned n,uint32_t pos){(void)ctx;return sceIoPread(apk_fd,out,n,pos);}
int acp_apk_stream_read(int fd,void *out,unsigned n){return acp_apk_read(cursors+fd,out,n,read_at,NULL);}
int64_t acp_apk_stream_seek(int fd,int64_t pos){return acp_apk_seek(cursors+fd,pos);}
int acp_apk_stream_close(int fd){cursors[fd]=(acp_apk_cursor){0};return 0;}
