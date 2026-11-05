#define _GNU_SOURCE
#include "startup_cache.h"
#include "startup_cache_id.h"
#include "acp_native.h"
#include "acp_sparse_cache.h"
#include "acp_sha256.h"
extern unsigned acp_obb_bytes; /* set at launch from the installed OBB */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
static acp_sparse_cache cache;
/* At most 1 MiB of read-ahead across all archive streams. A cache span ends
 * at a sparse-range boundary, so indexing cached headers never reads ahead
 * into asset payloads on disk. newlib owns and frees each stdio buffer. */
#define CACHE_STREAM_LIMIT 16u
#define CACHE_STREAM_BUFFER 65536u
static unsigned cache_streams;
struct acp_cache_file {FILE *backing;uint32_t position,backing_position,read_ahead;};
void acp_startup_cache_init(void){
    if(cache.bytes)return;
    FILE *f=fopen(ACP_DATA_PATH "/startup.cache","rb");
    if(!f){acp_log("Startup cache absent; streaming archive directly");return;}
    /* newlib's buffered fread refills in 1 KiB chunks, even for large reads.
     * Its unbuffered path reads directly into the caller's entire buffer. */
    setvbuf(f,NULL,_IONBF,0);
    unsigned char *data=malloc(ACP_STARTUP_CACHE_BYTES),digest[32];
    int valid=0;
    if(data&&fread(data,1,ACP_STARTUP_CACHE_BYTES,f)==ACP_STARTUP_CACHE_BYTES&&fgetc(f)==EOF&&!ferror(f)){
        acp_sha256 hash;acp_sha256_init(&hash);acp_sha256_update(&hash,data,ACP_STARTUP_CACHE_BYTES);acp_sha256_final(&hash,digest);
        if(!memcmp(digest,startup_cache_sha256,32)&&!acp_sparse_cache_init(&cache,data,ACP_STARTUP_CACHE_BYTES,acp_obb_bytes))valid=1;
    }
    fclose(f);
    if(!valid){free(data);acp_log("Startup cache invalid or unavailable; streaming archive directly");}
    else acp_log("Verified startup cache: %u bytes, %u ranges",ACP_STARTUP_CACHE_BYTES,cache.count);
}
static ssize_t cache_read(void *opaque,char *buffer,size_t bytes){
    acp_cache_file *f=opaque;const unsigned char *data;
    size_t n=acp_sparse_cache_span(&cache,f->position,bytes,&data);
    if(!n)return 0;
    if(data)memcpy(buffer,data,n);
    else{
        /* Begin conservatively after a seek. A random header probe must not
         * fetch 64 KiB it will discard; contiguous payload reads ramp up. */
        if(n>f->read_ahead)n=f->read_ahead;
        if(f->backing_position!=f->position){
            if(fseek(f->backing,(long)f->position,SEEK_SET))return -1;
            f->backing_position=f->position;
        }
        n=fread(buffer,1,n,f->backing);
        f->backing_position+=(uint32_t)n;
        if(!n&&ferror(f->backing))return -1;
        if(n && f->read_ahead<CACHE_STREAM_BUFFER)f->read_ahead*=4u;
    }
    f->position+=(uint32_t)n;return (ssize_t)n;
}
static int cache_seek(void *opaque,off_t *offset,int whence){
    acp_cache_file *f=opaque;
    int64_t base;
    if(whence==SEEK_SET)base=0;
    else if(whence==SEEK_CUR)base=f->position;
    else if(whence==SEEK_END)base=cache.source_bytes;
    else{errno=EINVAL;return -1;}
    int64_t next=base+*offset;
    if(base<0||next<0||next>INT32_MAX){errno=EINVAL;return -1;}
    if((uint32_t)next!=f->position)f->read_ahead=1024;
    f->position=(uint32_t)next;*offset=(off_t)next;return 0;
}
static int cache_close(void *opaque){
    acp_cache_file *f=opaque;int result=f->backing?fclose(f->backing):0;
    __atomic_fetch_sub(&cache_streams,1,__ATOMIC_RELAXED);free(f);return result;
}
FILE *acp_startup_cache_file(FILE *backing,acp_cache_file **cookie){
    *cookie=NULL;
    if(!backing||!cache.bytes)return backing;
    unsigned active=__atomic_load_n(&cache_streams,__ATOMIC_RELAXED);
    do{if(active>=CACHE_STREAM_LIMIT)return backing;}
    while(!__atomic_compare_exchange_n(&cache_streams,&active,active+1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    acp_cache_file *c=calloc(1,sizeof(*c));
    if(!c){__atomic_fetch_sub(&cache_streams,1,__ATOMIC_RELAXED);return backing;}
    long position=ftell(backing);
    if(position<0){free(c);__atomic_fetch_sub(&cache_streams,1,__ATOMIC_RELAXED);return backing;}
    c->backing=backing;c->position=c->backing_position=(uint32_t)position;c->read_ahead=1024;
    cookie_io_functions_t io={.read=cache_read,.seek=cache_seek,.close=cache_close};
    FILE *filtered=fopencookie(c,"rb",io);
    if(!filtered){free(c);__atomic_fetch_sub(&cache_streams,1,__ATOMIC_RELAXED);return backing;}
    setvbuf(filtered,NULL,_IOFBF,CACHE_STREAM_BUFFER);
    setvbuf(backing,NULL,_IONBF,0);
    *cookie=c;return filtered;
}
FILE *acp_startup_cache_detach(FILE *filtered,acp_cache_file *c){
    long position=ftell(filtered);if(position<0||fseek(c->backing,position,SEEK_SET))return NULL;
    FILE *backing=c->backing;c->backing=NULL;fclose(filtered);return backing;
}
