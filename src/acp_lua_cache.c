#include "acp_lua_cache.h"
#include <stdio.h>
#include <string.h>

/* File: 8-byte magic, 8-byte build tag, then records. A record is a 24-byte
 * header (magic, source size, code size, code checksum, 64-bit key) followed
 * by the code, padded to four bytes. Only the game thread uses the cache.
 *
 * The file is never held in memory: Vita graphics initialization needs its
 * free-memory headroom, and 00.61's resident 6 MB image consumed it. Records
 * are read through one bounded window; chunks load in roughly the order they
 * were stored, so consecutive hits are served from the same window. */
#define FILE_MAGIC "ACPLBC1"
#define RECORD_MAGIC 0x5243424cu
#define SLOTS 8192u
#define WINDOW (256u*1024)
typedef struct {uint32_t magic,source_size,code_size,check;uint64_t key;} Record;
typedef struct {uint64_t key;uint32_t source_size,offset,code_size,state;} Slot;
static Slot slots[SLOTS];
static unsigned char *window,*large;
static uint32_t window_start,window_bytes,large_capacity,file_bytes,limit;
static FILE *in,*out;
static char cache_path[256];
static acp_lua_cache_memory memory;
static acp_lua_cache_stats stats;

static uint32_t rotl(uint32_t x,int r){return (x<<r)|(x>>(32-r));}
static uint32_t word(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint32_t xxh32(const void *data,size_t n,uint32_t seed){
    const uint32_t p1=2654435761u,p2=2246822519u,p3=3266489917u,p4=668265263u,p5=374761393u;
    const unsigned char *p=data,*end=p+n;uint32_t h;
    if(n>=16){
        uint32_t v1=seed+p1+p2,v2=seed+p2,v3=seed,v4=seed-p1;
        for(const unsigned char *limit16=end-16;p<=limit16;p+=16){
            v1=rotl(v1+word(p)*p2,13)*p1;v2=rotl(v2+word(p+4)*p2,13)*p1;
            v3=rotl(v3+word(p+8)*p2,13)*p1;v4=rotl(v4+word(p+12)*p2,13)*p1;
        }
        h=rotl(v1,1)+rotl(v2,7)+rotl(v3,12)+rotl(v4,18);
    }else h=seed+p5;
    h+=(uint32_t)n;
    for(;p+4<=end;p+=4)h=rotl(h+word(p)*p3,17)*p4;
    for(;p<end;p++)h=rotl(h+*p*p5,11)*p1;
    h^=h>>15;h*=p2;h^=h>>13;h*=p3;h^=h>>16;
    return h;
}
uint64_t acp_lua_cache_key(const char *name,const void *source,size_t size){
    uint32_t n=name?xxh32(name,strlen(name),0x9e3779b1u):0;
    return ((uint64_t)xxh32(source,size,n^0x165667b1u)<<32)|xxh32(source,size,n+0x27d4eb2fu);
}
static Slot *slot_for(uint64_t key,uint32_t source_size,int insert){
    for(uint32_t i=0;i<SLOTS;i++){
        Slot *s=&slots[((uint32_t)key+i*0x9e3779b1u)&(SLOTS-1)];
        if(s->state&&s->key==key&&s->source_size==source_size)return s;
        if(!s->state)return insert?s:NULL;
    }
    return NULL;
}
/* Bytes [offset,offset+size) of the input file, valid until the next read. */
static const unsigned char *read_range(uint32_t offset,uint32_t size){
    if(!in||!window||offset>file_bytes||size>file_bytes-offset)return NULL;
    if(offset>=window_start&&size<=window_bytes&&offset-window_start<=window_bytes-size)
        return window+(offset-window_start);
    unsigned char *target=window;uint32_t want=WINDOW;
    if(size>WINDOW){
        if(size>large_capacity){
            if(large)memory.release(large);
            large=memory.allocate(size);large_capacity=large?size:0;
            if(!large)return NULL;
        }
        target=large;want=size;
    }else if(want>file_bytes-offset)want=file_bytes-offset;
    if(fseek(in,(long)offset,SEEK_SET)||fread(target,1,want,in)!=want){window_bytes=0;return NULL;}
    if(target==window){window_start=offset;window_bytes=want;}
    else window_bytes=0;
    return target;
}
/* newlib's buffered fread refills 1 KiB at a time even for large reads:
 * indexing an 8 MB cache took ~8000 storage calls (2.9 s on hardware).
 * Unbuffered, each window is one read straight into its buffer. */
static FILE *open_input(const char *path){
    FILE *f=fopen(path,"rb");
    if(f)setvbuf(f,NULL,_IONBF,0);
    return f;
}
/* Index valid records in [16,size); returns the end of the valid prefix. */
static uint32_t index_records(uint32_t size){
    uint32_t at=16;
    while(size-at>=(uint32_t)sizeof(Record)){
        const unsigned char *h=read_range(at,(uint32_t)sizeof(Record));
        if(!h)break;
        Record r;memcpy(&r,h,sizeof(r));
        uint32_t body=at+(uint32_t)sizeof(r);
        if(r.magic!=RECORD_MAGIC||!r.code_size||r.code_size>size-body)break;
        const unsigned char *code=read_range(body,r.code_size);
        if(!code||xxh32(code,r.code_size,0x5bd1e995u)!=r.check)break;
        Slot *s=slot_for(r.key,r.source_size,1);
        if(!s)break;
        if(!s->state){s->key=r.key;s->source_size=r.source_size;s->offset=body;s->code_size=r.code_size;s->state=1;}
        uint32_t padded=(r.code_size+3u)&~3u;
        if(padded>size-body)break;
        at=body+padded;
    }
    return at;
}
/* Replace the file with its first valid bytes through a temporary copy. */
static int keep_prefix(uint32_t valid){
    char temp[sizeof(cache_path)+8];
    snprintf(temp,sizeof(temp),"%s.tmp",cache_path);
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    int ok=1;
    for(uint32_t at=0;ok&&at<valid;){
        uint32_t n=valid-at>WINDOW?WINDOW:valid-at;
        const unsigned char *p=read_range(at,n);
        ok=p&&fwrite(p,1,n,f)==n;at+=n;
    }
    if(fflush(f))ok=0;
    if(fclose(f))ok=0;
    if(in){fclose(in);in=NULL;}
    window_bytes=0;
    if(!ok){remove(temp);return -1;}
    remove(cache_path);
    if(rename(temp,cache_path)){remove(temp);return -1;}
    return 0;
}
static void start_empty(const unsigned char header[16]){
    memset(slots,0,sizeof(slots));
    if(in){fclose(in);in=NULL;}
    out=fopen(cache_path,"wb");file_bytes=0;
    if(out&&(fwrite(header,1,16,out)!=16||fflush(out))){fclose(out);out=NULL;}
    if(out)file_bytes=16;
}
void acp_lua_cache_open(const char *path,const char tag[8],uint32_t file_limit,const acp_lua_cache_memory *m){
    acp_lua_cache_close();
    memory=*m;limit=file_limit;
    snprintf(cache_path,sizeof(cache_path),"%s",path);
    unsigned char header[16];memcpy(header,FILE_MAGIC,8);memcpy(header+8,tag,8);
    window=memory.allocate(WINDOW);
    if(!window)return;
    in=open_input(path);
    uint32_t size=0;
    if(in&&!fseek(in,0,SEEK_END)){long n=ftell(in);if(n>=16&&(unsigned long)n<=limit)size=(uint32_t)n;}
    file_bytes=size;
    const unsigned char *h=size?read_range(0,16):NULL;
    if(!h||memcmp(h,header,16)){start_empty(header);return;}
    uint32_t valid=index_records(size);
    stats.loaded_bytes=valid;
    if(valid!=size){
        /* Drop a damaged tail; earlier records stay valid and indexed. */
        if(keep_prefix(valid)){start_empty(header);stats.loaded_bytes=0;return;}
        in=open_input(path);file_bytes=valid;
        if(!in){start_empty(header);stats.loaded_bytes=0;return;}
    }
    out=fopen(path,"ab");
    if(out)file_bytes=valid;
}
void acp_lua_cache_close(void){
    if(out){fclose(out);out=NULL;}
    if(in){fclose(in);in=NULL;}
    if(window){memory.release(window);window=NULL;}
    if(large){memory.release(large);large=NULL;}
    large_capacity=window_start=window_bytes=file_bytes=0;
    memset(slots,0,sizeof(slots));memset(&stats,0,sizeof(stats));
}
const void *acp_lua_cache_find(uint64_t key,uint32_t source_size,uint32_t *code_size){
    Slot *s=slot_for(key,source_size,0);
    if(!s||s->state!=1){stats.misses++;return NULL;}
    /* Appends never move indexed records; recheck the bytes actually read. */
    const unsigned char *header=read_range(s->offset-(uint32_t)sizeof(Record),(uint32_t)sizeof(Record));
    Record r;
    if(!header){stats.misses++;return NULL;}
    memcpy(&r,header,sizeof(r));
    const unsigned char *code=read_range(s->offset,s->code_size);
    if(!code||xxh32(code,s->code_size,0x5bd1e995u)!=r.check){s->state=2;stats.rejected++;stats.misses++;return NULL;}
    stats.hits++;*code_size=s->code_size;return code;
}
void acp_lua_cache_reject(uint64_t key,uint32_t source_size){
    Slot *s=slot_for(key,source_size,0);
    if(s&&s->state==1){s->state=2;stats.rejected++;}
}
void acp_lua_cache_store(uint64_t key,uint32_t source_size,const void *code,uint32_t code_size){
    if(!out||!code_size)return;
    Slot *s=slot_for(key,source_size,1);
    if(!s||s->state)return; /* present, rejected, or already stored this session */
    uint32_t padded=(code_size+3u)&~3u;
    if(padded>limit-(uint32_t)sizeof(Record)||file_bytes>limit-(uint32_t)sizeof(Record)-padded)return;
    Record r={RECORD_MAGIC,source_size,code_size,xxh32(code,code_size,0x5bd1e995u),key};
    static const unsigned char zero[3];
    if(fwrite(&r,1,sizeof(r),out)!=sizeof(r)||fwrite(code,1,code_size,out)!=code_size||
       fwrite(zero,1,padded-code_size,out)!=padded-code_size||fflush(out)){
        /* A partial record is discarded by the next open's validation. */
        fclose(out);out=NULL;return;
    }
    s->key=key;s->source_size=source_size;s->state=3;
    file_bytes+=(uint32_t)sizeof(r)+padded;stats.stored++;
}
acp_lua_cache_stats acp_lua_cache_stats_get(void){stats.file_bytes=file_bytes;return stats;}
