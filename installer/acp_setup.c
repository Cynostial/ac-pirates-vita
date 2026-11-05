#define _POSIX_C_SOURCE 200809L
/* First-launch setup from the player's own APK and OBB, from any source: the
 * game is identified by its content, never by the container's bytes.
 * - APK: the members setup needs are found by name in its central directory;
 *   each unpacked file must match its compiled size, CRC and SHA-256
 *   (libSparkApp.so byte for byte, as the port patches it). Output names
 *   come only from the compiled table. With another layout than the pinned
 *   APK's, every asset is unpacked too (the game then reads loose files).
 * - OBB: its entry names identify the version; the archive is rewritten in
 *   the original order exactly as tools/build_plain_obb.py writes it, each
 *   .spd entry decrypted and tagged with the marker native/plain_assets.c
 *   trims, and the finished archive must match the compiled digest.
 * - Finished outputs survive a pause or power loss and are reused after
 *   their digest is checked again. Saves (userdata/) are never touched. */
#include "acp_setup.h"
#include "../src/acp_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MAKE_DIR(p) _mkdir(p)
#define SYNC_FILE(f) _commit(_fileno(f))
#else
#define MAKE_DIR(p) mkdir(p,0777)
#define SYNC_FILE(f) fsync(fileno(f))
#endif
#define CHUNK (1u<<20)
#define PAUSED (-2)
#define UNSUPPORTED (-3)
#define DAMAGED (-4)
#define BROKEN_PACKAGE (-5)
static const char MARKER[16]="ACP-VITA-PLAIN1";   /* 15 characters and NUL */
static const char READY_MAGIC[8]={'A','C','P','S','E','T','P','2'};
const char *acp_setup_order_path=ACP_SETUP_ORDER;

typedef struct { acp_setup_progress callback;void *arg;uint64_t base,total;const char *phase; } progress;
static int report(progress *p,uint64_t done){
    return p->callback&&p->callback(p->phase,p->base+done,p->total,p->arg)?PAUSED:0;
}
static int fail(acp_setup_result *r,const char *message){snprintf(r->message,sizeof(r->message),"%s",message);return -1;}
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put16(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}

static int join(char out[512],const char *root,const char *rel){
    if(!root||!*root||!rel||!*rel||*rel=='/'||strchr(rel,':')||strchr(rel,'\\')||strstr(rel,".."))return -1;
    int n=snprintf(out,512,"%s/%s",root,rel);return n<0||n>=512?-1:0;
}
static int sized(const char *path,uint64_t size){
    struct stat s;return !stat(path,&s)&&S_ISREG(s.st_mode)&&s.st_size>=0&&(uint64_t)s.st_size==size;
}
static int exists(const char *path){struct stat s;return !stat(path,&s)&&S_ISREG(s.st_mode);}
static int parents(const char *path){
    char p[512];if(strlen(path)>=sizeof(p))return -1;strcpy(p,path);
    char *start=strchr(p,':');start=start?start+1:p;if(*start=='/')++start;
    for(char *s=start;*s;++s)if(*s=='/'){
        *s=0;struct stat st;
        if(stat(p,&st)){if(MAKE_DIR(p))return -1;}
        else if(!S_ISDIR(st.st_mode))return -1;
        *s='/';
    }
    return 0;
}
/* Unbuffered handles: every transfer here is already large or batched
 * (newlib's default buffering refills in 1 KiB steps on the Vita). */
static FILE *open_file(const char *path,const char *mode){
    FILE *f=fopen(path,mode);
    if(f&&setvbuf(f,NULL,_IONBF,0)){fclose(f);return NULL;}
    return f;
}
static int finish(FILE *f){
    int result=fflush(f);if(!result)result=SYNC_FILE(f);
    if(fclose(f))result=-1;
    return result?-1:0;
}
static int publish(const char *temp,const char *path){
    if(!rename(temp,path))return 0;
    /* Vita and Windows file systems do not rename over an existing file. The
     * temp file is complete and verified; a crash here leaves a missing file
     * that the next run rebuilds. */
    if(exists(path)&&!remove(path)&&!rename(temp,path))return 0;
    return -1;
}
static int read_at(FILE *f,uint64_t offset,void *out,size_t n){
    return fseek(f,(long)offset,SEEK_SET)||fread(out,1,n,f)!=n?-1:0;
}

/* ---- digests ---- */
static int hash_file(const char *path,uint64_t size,const uint8_t expected[32],uint8_t *buffer,progress *p){
    if(!sized(path,size))return 0;
    FILE *f=open_file(path,"rb");if(!f)return 0;
    acp_sha256 s;acp_sha256_init(&s);uint64_t done=0;int rc=0;
    while(done<size){
        if((rc=report(p,done)))break;
        size_t n=size-done>CHUNK?CHUNK:(size_t)(size-done);
        if(fread(buffer,1,n,f)!=n){rc=-1;break;}
        acp_sha256_update(&s,buffer,n);done+=n;
    }
    fclose(f);if(rc==PAUSED)return PAUSED;if(rc)return 0;
    uint8_t digest[32];acp_sha256_final(&s,digest);
    return !memcmp(digest,expected,32);
}
static int write_verified(const char *path,const uint8_t *data,size_t size,const uint8_t expected[32]){
    uint8_t digest[32];acp_sha256 s;acp_sha256_init(&s);acp_sha256_update(&s,data,size);acp_sha256_final(&s,digest);
    if(memcmp(digest,expected,32))return -1;
    char temp[512];if(snprintf(temp,sizeof(temp),"%s.part",path)>=(int)sizeof(temp)||parents(temp))return -1;
    FILE *f=open_file(temp,"wb");if(!f)return -1;
    int ok=fwrite(data,1,size,f)==size;if(finish(f))ok=0;
    if(!ok||publish(temp,path)){remove(temp);return -1;}
    return 0;
}

/* ---- ZIP central directories ---- */
typedef struct { const uint8_t *name;uint32_t n,method,flags,packed,size,crc,local,stamp; } member;
typedef struct { uint8_t *data;member *list;uint32_t count,start;uint64_t file; } zipdir;
static void zip_free(zipdir *z){free(z->data);free(z->list);memset(z,0,sizeof(*z));}
static int file_size(FILE *f,uint64_t *size){
    if(fseek(f,0,SEEK_END))return -1;
    long n=ftell(f);if(n<0)return -1;
    *size=(uint64_t)n;return 0;
}
/* The end record is searched from the back (it may carry a comment). Entries
 * point into the directory copy; ZIP64 and multi-disk archives are refused. */
static int zip_open(FILE *f,zipdir *z){
    memset(z,0,sizeof(*z));
    uint64_t file;if(file_size(f,&file)||file<22||file>0x7fffffffu)return -1;
    uint32_t tail=file<65557?(uint32_t)file:65557u;
    uint8_t *t=malloc(tail);if(!t)return -1;
    int rc=-1;uint32_t count=0,size=0,start=0,found=0;
    if(read_at(f,file-tail,t,tail))goto done;
    for(uint32_t i=tail-22;!found;i--){
        if(u32(t+i)==0x06054b50&&i+22+u16(t+i+20)==tail){
            count=u16(t+i+10);size=u32(t+i+12);start=u32(t+i+16);found=1;
            if(u16(t+i+4)||u16(t+i+6)||u16(t+i+8)!=count||(uint64_t)start+size>file-tail+i)goto done;
        }
        if(!i)break;
    }
    if(!found)goto done;
    z->data=malloc(size?size:1);z->list=malloc(sizeof(member)*(count?count:1));
    if(!z->data||!z->list||read_at(f,start,z->data,size))goto done;
    for(uint32_t k=0,o=0;k<count;k++){
        if(o+46>size||u32(z->data+o)!=0x02014b50)goto done;
        const uint8_t *d=z->data+o;uint32_t n=u16(d+28),next=o+46+n+u16(d+30)+u16(d+32);
        if(next>size)goto done;
        z->list[k]=(member){d+46,n,u16(d+10),u16(d+8),u32(d+20),u32(d+24),u32(d+16),u32(d+42),u32(d+12)};
        o=next;
    }
    z->count=count;z->start=start;z->file=file;rc=0;
done:
    free(t);if(rc)zip_free(z);
    return rc;
}
static const member *zip_find(const zipdir *z,const char *name){
    size_t n=strlen(name);
    for(uint32_t i=0;i<z->count;i++)if(z->list[i].n==n&&!memcmp(z->list[i].name,name,n))return &z->list[i];
    return NULL;
}
/* A member's content, stored or deflated, read in pieces. */
typedef struct { FILE *f;uint32_t left;int deflated,ended;uint8_t *in;z_stream z; } unpacker;
static int unpack_open(unpacker *u,FILE *f,const zipdir *zd,const member *m,uint8_t *in){
    memset(u,0,sizeof(*u));uint8_t h[30];
    if((m->flags&1)||(m->method!=0&&m->method!=8)||read_at(f,m->local,h,30)||u32(h)!=0x04034b50)return -1;
    uint64_t at=(uint64_t)m->local+30+u16(h+26)+u16(h+28);
    if(at+m->packed>zd->file||fseek(f,(long)at,SEEK_SET))return -1;
    u->f=f;u->left=m->packed;u->in=in;u->deflated=m->method==8;
    if(!u->deflated&&m->packed!=m->size)return -1;
    if(u->deflated&&inflateInit2(&u->z,-MAX_WBITS)!=Z_OK)return -1;
    return 0;
}
/* The next piece into out: its length, 0 at the end, -1 on a read or format error. */
static long unpack_next(unpacker *u,uint8_t *out,size_t cap){
    if(!u->deflated){
        if(!u->left)return 0;
        size_t n=u->left>cap?cap:u->left;
        if(fread(out,1,n,u->f)!=n)return -1;
        u->left-=(uint32_t)n;return (long)n;
    }
    while(!u->ended){
        if(!u->z.avail_in&&u->left){
            uint32_t take=u->left>CHUNK?CHUNK:u->left;
            if(fread(u->in,1,take,u->f)!=take)return -1;
            u->left-=take;u->z.next_in=u->in;u->z.avail_in=take;
        }
        u->z.next_out=out;u->z.avail_out=(uInt)cap;
        uLong before=u->z.total_in;int code=inflate(&u->z,Z_NO_FLUSH);
        size_t n=cap-u->z.avail_out;
        if(code==Z_STREAM_END){u->ended=1;if(u->left||u->z.avail_in)return -1;}
        else if(code!=Z_OK||(!n&&before==u->z.total_in))return -1;
        if(n)return (long)n;
    }
    return 0;
}
static void unpack_close(unpacker *u){if(u->deflated)inflateEnd(&u->z);}
/* A whole member into out (its size), checked against its own CRC. */
static int unpack_all(FILE *f,const zipdir *zd,const member *m,uint8_t *out,uint8_t *in){
    unpacker u;if(unpack_open(&u,f,zd,m,in))return -1;
    uint32_t done=0;long n=0;
    while(done<m->size&&(n=unpack_next(&u,out+done,m->size-done))>0)done+=(uint32_t)n;
    if(n>=0&&done==m->size){uint8_t extra;n=unpack_next(&u,&extra,1);}
    unpack_close(&u);
    return n!=0||done!=m->size||(uint32_t)crc32(0,out,done)!=m->crc?-1:0;
}

/* ---- APK members ---- */
/* One member to path, checked against the expected size and CRC (and digest). */
static int extract(FILE *apk,const zipdir *zd,const member *m,const char *path,uint32_t bytes,uint32_t expected_crc,
                   const uint8_t *hash,uint8_t *in,uint8_t *out,progress *p){
    if(m->size!=bytes)return -1;
    char temp[512];if(snprintf(temp,sizeof(temp),"%s.part",path)>=(int)sizeof(temp)||parents(temp))return -1;
    FILE *f=open_file(temp,"wb");if(!f)return -1;
    unpacker u;if(unpack_open(&u,apk,zd,m,in)){fclose(f);remove(temp);return -1;}
    acp_sha256 s;acp_sha256_init(&s);uLong crc=crc32(0,NULL,0);
    int rc=0;uint32_t done=0;
    for(;;){
        if(report(p,done)){rc=PAUSED;break;}
        long n=unpack_next(&u,out,CHUNK);
        if(n<0){rc=-1;break;}
        if(!n)break;
        if((uint32_t)n>bytes-done||fwrite(out,1,(size_t)n,f)!=(size_t)n){rc=-1;break;}
        acp_sha256_update(&s,out,(size_t)n);crc=crc32(crc,out,(uInt)n);done+=(uint32_t)n;
    }
    unpack_close(&u);
    uint8_t digest[32];acp_sha256_final(&s,digest);
    if(!rc&&(done!=bytes||(uint32_t)crc!=expected_crc||(hash&&memcmp(digest,hash,32))))rc=-1;
    if(finish(f)&&!rc)rc=-1;
    if(!rc&&publish(temp,path))rc=-1;
    if(rc)remove(temp);
    return rc;
}
/* The game reads stored assets straight from game.apk only when its size and
 * central directory are the pinned ones (native/apk_stream.c); otherwise it
 * reads every asset from a loose file. */
static int pinned_layout(FILE *apk,const zipdir *zd){
    if(zd->file!=acp_setup_apk_size)return 0;
    uint32_t n=(uint32_t)(zd->file-zd->start);uint8_t *t=malloc(n);int same=0;
    if(t&&!read_at(apk,zd->start,t,n)){
        uint8_t digest[32];acp_sha256 s;acp_sha256_init(&s);acp_sha256_update(&s,t,n);acp_sha256_final(&s,digest);
        same=!memcmp(digest,acp_setup_apk_directory_hash,32);
    }
    free(t);return same;
}
static int is_table_file(const char *name){
    for(unsigned i=0;i<acp_setup_file_count;i++)if(!strcmp(acp_setup_files[i].path,name))return 1;
    return 0;
}
/* An asset of the player's APK that only the pinned layout serves from game.apk. */
static int loose_asset(const member *m,char name[512]){
    if(m->n<8||m->n>=512||memcmp(m->name,"assets/",7)||m->name[m->n-1]=='/'||memchr(m->name,0,m->n))return 0;
    memcpy(name,m->name,m->n);name[m->n]=0;
    return !is_table_file(name);
}

/* ---- platform.args: the APK's default string resources ---- */
static size_t mutf8(uint8_t *out,uint32_t unit){
    if(unit&&unit<0x80){out[0]=(uint8_t)unit;return 1;}
    if(unit<0x800){out[0]=(uint8_t)(0xc0|unit>>6);out[1]=(uint8_t)(0x80|(unit&63));return 2;}
    out[0]=(uint8_t)(0xe0|unit>>12);out[1]=(uint8_t)(0x80|((unit>>6)&63));out[2]=(uint8_t)(0x80|(unit&63));return 3;
}
/* UTF-8 pool string -> Java modified UTF-8 (UTF-16 units, surrogates apart). */
static int convert_string(const uint8_t *s,size_t n,uint8_t *out,size_t cap,size_t *used){
    size_t o=0;
    for(size_t i=0;i<n;){
        uint32_t c=s[i],extra=c<0x80?0:c>=0xf0?3:c>=0xe0?2:c>=0xc0?1:4;
        if(extra==4||i+extra>=n+(extra?0:1))return -1;
        if(extra)c&=0x3f>>extra;
        for(uint32_t k=1;k<=extra;k++){if((s[i+k]&0xc0)!=0x80)return -1;c=c<<6|(s[i+k]&63);}
        i+=1+extra;
        if(o+6>cap)return -1;
        if(c>=0x10000){c-=0x10000;o+=mutf8(out+o,0xd800|(c>>10));o+=mutf8(out+o,0xdc00|(c&0x3ff));}
        else o+=mutf8(out+o,c);
    }
    *used=o;return 0;
}
static int build_args(const uint8_t *table,uint8_t **blob_out,size_t *size_out){
    uint8_t *blob=malloc(65536);int rc=-1;
    if(!blob)goto done;
    uint32_t header=u16(table+2),size=acp_setup_arsc_size;
    if(header+28>size)goto done;
    const uint8_t *pool=table+header;
    uint32_t pool_header=u16(pool+2),count=u32(pool+8),flags=u32(pool+16),strings=u32(pool+20);
    if(!(flags&0x100)||header+pool_header+4ull*count>size)goto done;
    memcpy(blob,"ACPA\1\0",6);put16(blob+6,acp_setup_string_count);size_t o=8;
    for(unsigned i=0;i<acp_setup_string_count;i++){
        const acp_setup_string *e=&acp_setup_strings[i];
        if(e->index>=count)goto done;
        uint64_t p=(uint64_t)header+strings+u32(pool+pool_header+4*e->index);
        if(p+4>size)goto done;
        p+=table[p]&0x80?2:1;
        uint32_t n=table[p++];if(n&0x80)n=(n&0x7f)<<8|table[p++];
        if(p+n>size)goto done;
        size_t klen=strlen(e->key),vlen;
        if(o+4+klen+1>65536)goto done;
        memcpy(blob+o+4,e->key,klen);blob[o+4+klen]=0;
        if(convert_string(table+p,n,blob+o+5+klen,65536-(o+5+klen)-1,&vlen))goto done;
        put16(blob+o,(uint32_t)klen);put16(blob+o+2,(uint32_t)vlen);
        o+=5+klen+vlen;blob[o++]=0;
    }
    *blob_out=blob;*size_out=o;blob=NULL;rc=0;
done:
    free(blob);return rc;
}

/* ---- OBB conversion ---- */
typedef struct { FILE *f;uint8_t *buffer;size_t used;uint64_t position;acp_sha256 sha; } writer;
static int flush_writer(writer *w){
    if(w->used&&fwrite(w->buffer,1,w->used,w->f)!=w->used)return -1;
    w->used=0;return 0;
}
static int put(writer *w,const void *data,size_t n){
    acp_sha256_update(&w->sha,data,n);w->position+=n;
    if(n>=CHUNK/4){if(flush_writer(w))return -1;return fwrite(data,1,n,w->f)==n?0:-1;}
    if(w->used+n>CHUNK&&flush_writer(w))return -1;
    memcpy(w->buffer+w->used,data,n);w->used+=n;return 0;
}
/* The PKCS#7 pad length of an encrypted entry, or 0 if it is not one. */
static uint32_t padding(const uint8_t *data,uint32_t size){
    if(!size||size%16)return 0;
    /* Last plaintext block = D(C[n]) xor C[n-1] (the zero IV for one block). */
    uint8_t last[16],previous[16]={0};
    memcpy(last,data+size-16,16);
    if(size>=32)memcpy(previous,data+size-32,16);
    acp_setup_aes_cbc_decrypt(last,16);
    for(int i=0;i<16;i++)last[i]^=previous[i];
    uint32_t pad=last[15];
    if(pad<1||pad>16)return 0;
    for(uint32_t i=0;i<pad;i++)if(last[15-i]!=pad)return 0;
    return pad;
}
static const member *sorting;
static int by_name(const void *a,const void *b){
    const member *x=&sorting[*(const uint16_t *)a],*y=&sorting[*(const uint16_t *)b];
    int c=memcmp(x->name,y->name,x->n<y->n?x->n:y->n);
    return c?c:(x->n>y->n)-(x->n<y->n);
}
/* setup/obb-order.bin (tools/prepare_setup.py): for each entry of the original
 * archive in its order, its position among the names sorted bytewise (u16)
 * and its timestamp (u8 into a table of DOS date-times). */
static uint8_t *load_order(uint32_t *stamps,const uint8_t **entries){
    FILE *f=open_file(acp_setup_order_path,"rb");if(!f)return NULL;
    uint8_t *o=malloc(acp_setup_order_size+1);size_t n=o?fread(o,1,acp_setup_order_size+1,f):0;fclose(f);
    uint8_t digest[32];acp_sha256 s;acp_sha256_init(&s);if(o)acp_sha256_update(&s,o,n);acp_sha256_final(&s,digest);
    if(!o||n!=acp_setup_order_size||memcmp(digest,acp_setup_order_hash,32)||memcmp(o,"ACPORD1",8)||
       u32(o+8)!=acp_setup_obb_entries||14+4ull*u16(o+12)+3ull*acp_setup_obb_entries!=n){free(o);return NULL;}
    *stamps=u16(o+12);*entries=o+14+4u*u16(o+12);
    return o;
}
/* Any copy of the 2.9.1 OBB (entry order, timestamps, stored or deflated):
 * its names identify the version; the archive is written in the original
 * order as tools/build_plain_obb.py writes it, each encrypted .spd entry
 * decrypted and tagged with the marker native/plain_assets.c trims, and the
 * result must match the compiled digest. */
static int convert_obb(const char *source,const char *temp,progress *p){
    int rc=-1;FILE *in=open_file(source,"rb");FILE *out=NULL;
    uint8_t *order=NULL,*entry=NULL,*central=NULL,*inflate_in=NULL;uint16_t *sorted=NULL;
    zipdir zd={0};writer w={0};
    w.buffer=malloc(CHUNK);inflate_in=malloc(CHUNK);
    if(!in||!w.buffer||!inflate_in)goto done;
    if(zip_open(in,&zd)){rc=DAMAGED;goto done;}
    if(zd.count!=acp_setup_obb_entries){rc=UNSUPPORTED;goto done;}
    uint32_t count=zd.count;
    sorted=malloc(sizeof(*sorted)*count);if(!sorted)goto done;
    for(uint32_t i=0;i<count;i++)sorted[i]=(uint16_t)i;
    sorting=zd.list;qsort(sorted,count,sizeof(*sorted),by_name);
    {
        uint8_t digest[32];acp_sha256 s;acp_sha256_init(&s);
        for(uint32_t i=0;i<count;i++){acp_sha256_update(&s,zd.list[sorted[i]].name,zd.list[sorted[i]].n);acp_sha256_update(&s,"",1);}
        acp_sha256_final(&s,digest);
        if(memcmp(digest,acp_setup_obb_names_hash,32)){rc=UNSUPPORTED;goto done;}
    }
    uint32_t stamps=0;const uint8_t *positions=NULL;
    if(!(order=load_order(&stamps,&positions))){rc=BROKEN_PACKAGE;goto done;}
    /* Largest .spd (decrypted whole) and the new central directory's size. */
    uint64_t largest=0,central_size=0;
    for(uint32_t i=0;i<count;i++){
        const member *m=&zd.list[i];
        if((m->flags&1)||(m->method!=0&&m->method!=8)){rc=UNSUPPORTED;goto done;}
        if(m->n>=4&&!memcmp(m->name+m->n-4,".spd",4)&&m->size>largest)largest=m->size;
        central_size+=46+m->n;
    }
    entry=malloc((size_t)(largest+16>CHUNK?largest+16:CHUNK));central=malloc((size_t)central_size+22);
    out=open_file(temp,"wb");
    if(!entry||!central||!out)goto done;
    w.f=out;acp_sha256_init(&w.sha);
    uint8_t *c=central;uint64_t done_bytes=0;
    for(uint32_t k=0;k<count;k++){
        if((rc=report(p,done_bytes)))goto done;
        rc=-1;
        const uint8_t *e=positions+3*k;
        if(u16(e)>=count||e[2]>=stamps)goto done;
        const member *m=&zd.list[sorted[u16(e)]];
        uint32_t stamp=u32(order+14+4u*e[2]),crc=m->crc,size=m->size,written=size;
        int spd=m->n>=4&&!memcmp(m->name+m->n-4,".spd",4);
        const uint8_t *data=NULL;
        if(spd&&size){
            if(unpack_all(in,&zd,m,entry,inflate_in))goto done;
            uint32_t pad=padding(entry,size);
            if(pad){
                acp_setup_aes_cbc_decrypt(entry,size);
                written=size-pad;memcpy(entry+written,MARKER,16);written+=16;
                crc=(uint32_t)crc32(0,entry,written);
            }
            data=entry;
        }
        /* Python's zipfile record for writestr(ZipInfo(name,date_time)):
         * version 2.0, no flags, stored, no extra field. */
        uint8_t h[30];
        put32(h,0x04034b50);h[4]=20;h[5]=0;put16(h+6,0);put16(h+8,0);
        put32(h+10,stamp);put32(h+14,crc);put32(h+18,written);put32(h+22,written);put16(h+26,m->n);put16(h+28,0);
        uint32_t local_offset=(uint32_t)w.position;
        if(put(&w,h,30)||put(&w,m->name,m->n))goto done;
        if(data){if(put(&w,data,written))goto done;}
        else if(size){                              /* unchanged: streamed through */
            unpacker u;if(unpack_open(&u,in,&zd,m,inflate_in))goto done;
            uLong check=crc32(0,NULL,0);uint32_t got=0;long n;
            while((n=unpack_next(&u,entry,CHUNK))>0){
                if((uint32_t)n>size-got||put(&w,entry,(size_t)n)){n=-1;break;}
                check=crc32(check,entry,(uInt)n);got+=(uint32_t)n;
                if(got<size&&report(p,done_bytes)){unpack_close(&u);rc=PAUSED;goto done;}
            }
            unpack_close(&u);
            if(n<0||got!=size||(uint32_t)check!=crc)goto done;
        }
        /* The original archive's attributes: 0x10 for folders, 0x20 for files. */
        int folder=m->n&&m->name[m->n-1]=='/';
        put32(c,0x02014b50);c[4]=20;c[5]=0;c[6]=20;c[7]=0;put16(c+8,0);put16(c+10,0);
        put32(c+12,stamp);put32(c+16,crc);put32(c+20,written);put32(c+24,written);
        put16(c+28,m->n);put16(c+30,0);put16(c+32,0);put16(c+34,0);put16(c+36,0);
        put32(c+38,folder?0x10u:0x20u);put32(c+42,local_offset);
        memcpy(c+46,m->name,m->n);c+=46+m->n;
        done_bytes+=m->packed;
    }
    uint32_t start=(uint32_t)w.position;
    put32(c,0x06054b50);put16(c+4,0);put16(c+6,0);put16(c+8,count);put16(c+10,count);
    put32(c+12,(uint32_t)central_size);put32(c+16,start);put16(c+20,0);
    if(put(&w,central,(size_t)central_size+22)||flush_writer(&w))goto done;
    uint8_t digest[32];acp_sha256_final(&w.sha,digest);
    if(w.position!=acp_setup_plain_size||memcmp(digest,acp_setup_plain_hash,32)){rc=DAMAGED;goto done;}
    FILE *f=out;out=NULL;
    rc=finish(f)?-1:0;
done:
    if(out)fclose(out);
    if(in)fclose(in);
    zip_free(&zd);free(order);free(sorted);free(entry);free(central);free(w.buffer);free(inflate_in);
    if(rc)remove(temp);
    return rc;
}

/* ---- startup.cache (tools/build_startup_cache.py) ---- */
typedef struct { uint32_t start,end; } span;
static int build_cache(const char *obb,uint8_t **blob_out,size_t *size_out,progress *p){
    int rc=-1;FILE *f=open_file(obb,"rb");
    uint8_t *directory=NULL,*data=NULL,*blob=NULL;span *spans=NULL;
    if(!f||!sized(obb,acp_setup_plain_size))goto done;
    uint8_t end[22];
    if(read_at(f,acp_setup_plain_size-22,end,22)||u32(end)!=0x06054b50)goto done;
    uint32_t count=u16(end+10),cd_size=u32(end+12),cd_offset=u32(end+16);
    if((uint64_t)cd_offset+cd_size+22!=acp_setup_plain_size)goto done;
    uint32_t tail=cd_size+22;
    directory=malloc(tail);spans=malloc(sizeof(*spans)*(count+1));data=malloc(acp_setup_cache_size);
    if(!directory||!spans||!data||read_at(f,cd_offset,directory,tail))goto done;
    size_t used=0;
    for(uint32_t i=0,o=0;i<count;i++){
        if(!(i&255)&&(rc=report(p,(uint64_t)i*acp_setup_cache_size/count)))goto done;
        rc=-1;
        if(o+46>cd_size||u32(directory+o)!=0x02014b50)goto done;
        uint32_t n=u16(directory+o+28),size=u32(directory+o+20),offset=u32(directory+o+42);
        o+=46+n+u16(directory+o+30)+u16(directory+o+32);
        uint32_t want=30+n+(size<10?size:10);
        if(used+want+tail>acp_setup_cache_size||read_at(f,offset,data+used,want))goto done;
        uint32_t names=u16(data+used+26),extra=u16(data+used+28);
        if(names!=n||extra){        /* the local header differs from the directory */
            want=30+names+extra+(size<10?size:10);
            if(used+want+tail>acp_setup_cache_size||read_at(f,offset,data+used,want))goto done;
        }
        spans[i]=(span){offset,offset+want};used+=want;
    }
    memcpy(data+used,directory,tail);spans[count]=(span){cd_offset,acp_setup_plain_size};used+=tail;
    /* Segments are in file order and never overlap, so merged ranges are
     * contiguous runs of the concatenated data. */
    uint32_t merged=0;
    for(uint32_t i=0;i<=count;i++){
        if(i&&spans[i].start<spans[i-1].end)goto done;
        if(merged&&spans[i].start==spans[merged-1].end)spans[merged-1].end=spans[i].end;
        else spans[merged++]=spans[i];
    }
    size_t payload=24+12*(size_t)merged,total=payload+used;
    if(total!=acp_setup_cache_size||!(blob=malloc(total)))goto done;
    memcpy(blob,"ACPCACH1",8);put32(blob+8,acp_setup_plain_size);put32(blob+12,merged);
    put32(blob+16,(uint32_t)payload);put32(blob+20,(uint32_t)total);
    size_t at=0;
    for(uint32_t i=0;i<merged;i++){
        uint32_t length=spans[i].end-spans[i].start;
        put32(blob+24+12*i,spans[i].start);put32(blob+28+12*i,length);put32(blob+32+12*i,(uint32_t)(payload+at));
        at+=length;
    }
    if(at!=used)goto done;
    memcpy(blob+payload,data,used);
    *blob_out=blob;*size_out=total;blob=NULL;rc=0;
done:
    if(f)fclose(f);
    free(directory);free(data);free(blob);free(spans);
    return rc;
}

/* ---- ready marker ---- */
/* Magic, table identity, game.apk's size and whether the assets are loose. */
#define MARKER_BYTES 45
static int marker(const char *root,uint32_t *apk_size){
    char path[512];if(join(path,root,".setup-ready"))return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    uint8_t data[MARKER_BYTES+1];size_t n=fread(data,1,sizeof(data),f);fclose(f);
    if(n!=MARKER_BYTES||memcmp(data,READY_MAGIC,8)||memcmp(data+8,acp_setup_identity,32)||data[44]>1)return 0;
    *apk_size=u32(data+40);return 1;
}
static int write_marker(const char *root,uint32_t apk_size,int loose){
    char path[512];if(join(path,root,".setup-ready"))return -1;
    uint8_t data[MARKER_BYTES];memcpy(data,READY_MAGIC,8);memcpy(data+8,acp_setup_identity,32);
    put32(data+40,apk_size);data[44]=(uint8_t)!!loose;
    char temp[520];snprintf(temp,sizeof(temp),"%s.part",path);
    FILE *f=open_file(temp,"wb");if(!f)return -1;
    int ok=fwrite(data,1,MARKER_BYTES,f)==MARKER_BYTES;if(finish(f))ok=0;
    if(!ok||publish(temp,path)){remove(temp);return -1;}
    return 0;
}
int acp_setup_ready(const char *root){
    char path[512];uint32_t apk_size=0;
    if(!marker(root,&apk_size))return 0;
    if(join(path,root,ACP_SETUP_APK)||!sized(path,apk_size))return 0;
    for(unsigned i=0;i<acp_setup_file_count;i++)
        if(join(path,root,acp_setup_files[i].path)||!sized(path,acp_setup_files[i].bytes))return 0;
    if(join(path,root,"platform.args")||!sized(path,acp_setup_args_size))return 0;
    if(join(path,root,ACP_SETUP_OBB)||!sized(path,acp_setup_plain_size))return 0;
    if(join(path,root,"startup.cache")||!sized(path,acp_setup_cache_size))return 0;
    return 1;
}

/* ---- inputs ---- */
static int ends_with(const char *s,const char *suffix){
    size_t a=strlen(s),b=strlen(suffix);return a>=b&&!strcasecmp(s+a-b,suffix);
}
/* Any APK and OBB name and size: setup identifies the game by its content. */
int acp_setup_find(const char *root,acp_setup_result *r){
    r->apk[0]=r->obb[0]=0;r->plain_ready=0;r->message[0]=0;
    DIR *dir=opendir(root);if(!dir)return fail(r,"Cannot open ux0:data/acpirates/.");
    char apk[512]={0},obb[512]={0},canonical_apk[512]={0},canonical_obb[512]={0};
    unsigned apks=0,obbs=0;uint64_t canonical_size=0;struct dirent *e;
    while((e=readdir(dir))){
        int is_apk=ends_with(e->d_name,".apk"),is_obb=ends_with(e->d_name,".obb");
        char path[512];struct stat st;
        if((!is_apk&&!is_obb)||join(path,root,e->d_name)||stat(path,&st)||!S_ISREG(st.st_mode))continue;
        if(is_apk){if(!strcasecmp(e->d_name,ACP_SETUP_APK))strcpy(canonical_apk,path);else{apks++;strcpy(apk,path);}}
        else if(!strcasecmp(e->d_name,ACP_SETUP_OBB)){strcpy(canonical_obb,path);canonical_size=(uint64_t)st.st_size;}
        else{obbs++;strcpy(obb,path);}   /* also an original kept by an earlier PC install */
    }
    closedir(dir);
    if(apks>1)return fail(r,"Keep only one APK in ux0:data/acpirates/.");
    strcpy(r->apk,apks?apk:canonical_apk);
    if(!*r->apk)return fail(r,"No APK found. Copy your Assassin's Creed Pirates 2.9.1 APK into ux0:data/acpirates/.");
    r->plain_ready=*canonical_obb&&canonical_size==acp_setup_plain_size;
    if(*canonical_obb&&!r->plain_ready)strcpy(r->obb,canonical_obb);
    else if(obbs==1)strcpy(r->obb,obb);
    if(!r->plain_ready&&!*r->obb){
        if(obbs>1)return fail(r,"Keep only one OBB in ux0:data/acpirates/.");
        return fail(r,"No OBB found. Copy main.253.com.ubisoft.assassin.pirates.obb into ux0:data/acpirates/.");
    }
    return 0;
}
uint64_t acp_setup_space(const char *root,const acp_setup_result *r){
    (void)root;
    /* 16 MB slack, plus the assets an APK with another layout unpacks (8.4 MB). */
    uint64_t n=(16u<<20)+(10u<<20);
    for(unsigned i=0;i<acp_setup_file_count;i++)n+=acp_setup_files[i].bytes;
    n+=acp_setup_args_size+acp_setup_cache_size;
    if(!r->plain_ready)n+=acp_setup_plain_size;
    return n;
}

/* ---- run ---- */
#define NOT_SUPPORTED_APK "This APK is not supported. Use Assassin's Creed Pirates 2.9.1 for Android (ARMv7)."
/* The APK must hold the 2.9.1 ARMv7 game files setup unpacks (checked by the
 * directory first, then by content while unpacking). */
static int apk_supported(const zipdir *zd){
    for(unsigned i=0;i<acp_setup_file_count;i++){
        const member *m=zip_find(zd,acp_setup_files[i].member);
        if(!m||m->size!=acp_setup_files[i].bytes||m->crc!=acp_setup_files[i].crc)return 0;
    }
    const member *m=zip_find(zd,"resources.arsc");
    return m&&m->size==acp_setup_arsc_size;
}
int acp_setup_run(const char *root,acp_setup_result *r,acp_setup_progress callback,void *arg){
    r->completed=r->reused=r->loose=0;
    if(acp_setup_aes_selftest())return fail(r,"Setup self-test failed.");
    char apk_path[512],obb_path[512],ready[512],path[512],temp[520];
    if(join(apk_path,root,ACP_SETUP_APK)||join(obb_path,root,ACP_SETUP_OBB)||join(ready,root,".setup-ready"))
        return fail(r,"The game folder path is too long.");
    zipdir zd;
    FILE *apk=open_file(r->apk,"rb");
    if(!apk)return fail(r,"Cannot read the APK. Check the memory card.");
    if(zip_open(apk,&zd)){fclose(apk);return fail(r,NOT_SUPPORTED_APK);}
    int supported=apk_supported(&zd),pinned=supported&&pinned_layout(apk,&zd);
    uint32_t apk_size=(uint32_t)zd.file;
    zip_free(&zd);fclose(apk);
    if(!supported)return fail(r,NOT_SUPPORTED_APK);
    uint64_t obb_bytes=0;struct stat st;
    if(*r->obb&&!stat(r->obb,&st))obb_bytes=(uint64_t)st.st_size;
    /* From here on a missing marker keeps the game from starting on partial files. */
    if(remove(ready)&&errno!=ENOENT)return fail(r,"Cannot update ux0:data/acpirates/. Check the memory card.");
    if(strcasecmp(r->apk,apk_path)){
        if((exists(apk_path)&&remove(apk_path))||rename(r->apk,apk_path))return fail(r,"Cannot rename the APK to game.apk. Check the memory card.");
        strcpy(r->apk,apk_path);
    }
    uint8_t *buffers=malloc(2*CHUNK),*arsc=NULL;if(!buffers)return fail(r,"Not enough memory for setup.");
    apk=open_file(apk_path,"rb");
    if(!apk||zip_open(apk,&zd)){if(apk)fclose(apk);free(buffers);return fail(r,"Cannot read game.apk. Check the memory card.");}
    uint64_t files=0,loose_bytes=0;char name[512];
    for(unsigned i=0;i<acp_setup_file_count;i++)files+=acp_setup_files[i].bytes;
    if(!pinned)for(uint32_t i=0;i<zd.count;i++)if(loose_asset(&zd.list[i],name))loose_bytes+=zd.list[i].size;
    progress p={callback,arg,0,files+acp_setup_args_size+loose_bytes+
                (r->plain_ready?acp_setup_plain_size:obb_bytes)+acp_setup_cache_size,"Unpacking game files"};
    int rc=0;
    for(unsigned i=0;!rc&&i<acp_setup_file_count;i++){
        const acp_setup_file *e=&acp_setup_files[i];
        const member *m=zip_find(&zd,e->member);
        if(!m||join(path,root,e->path)){rc=-1;break;}
        int reuse=hash_file(path,e->bytes,e->hash,buffers,&p);
        if(reuse==PAUSED){rc=PAUSED;break;}
        if(reuse)r->reused++;
        else if(!(rc=extract(apk,&zd,m,path,e->bytes,e->crc,e->hash,buffers,buffers+CHUNK,&p)))r->completed++;
        if(!rc)p.base+=e->bytes;
    }
    if(!rc){
        if(join(path,root,"platform.args"))rc=-1;
        else{
            int reuse=hash_file(path,acp_setup_args_size,acp_setup_args_hash,buffers,&p);
            if(reuse==PAUSED)rc=PAUSED;
            else if(reuse)r->reused++;
            else{
                const member *m=zip_find(&zd,"resources.arsc");
                uint8_t digest[32];acp_sha256 s;
                arsc=m?malloc(acp_setup_arsc_size):NULL;
                rc=!arsc||unpack_all(apk,&zd,m,arsc,buffers)?-1:0;
                if(!rc){acp_sha256_init(&s);acp_sha256_update(&s,arsc,acp_setup_arsc_size);acp_sha256_final(&s,digest);
                        if(memcmp(digest,acp_setup_arsc_hash,32))rc=-1;}
                uint8_t *blob=NULL;size_t size=0;
                if(!rc)rc=build_args(arsc,&blob,&size)||size!=acp_setup_args_size||write_verified(path,blob,size,acp_setup_args_hash)?-1:0;
                free(blob);free(arsc);arsc=NULL;
                if(!rc)r->completed++;
            }
            if(!rc)p.base+=acp_setup_args_size;
        }
    }
    /* Another APK layout: the game reads every asset from a loose file. */
    for(uint32_t i=0;!rc&&!pinned&&i<zd.count;i++){
        const member *m=&zd.list[i];
        if(!loose_asset(m,name))continue;
        if(join(path,root,name)){rc=-1;break;}
        if(sized(path,m->size))r->reused++;
        else if(!(rc=extract(apk,&zd,m,path,m->size,m->crc,NULL,buffers,buffers+CHUNK,&p)))r->completed++;
        if(!rc){r->loose++;p.base+=m->size;}
    }
    zip_free(&zd);fclose(apk);
    if(rc==PAUSED)goto paused;
    if(rc){free(buffers);return fail(r,"Unpacking the APK failed. Check free space and the memory card, then try again.");}

    int plain=0;
    if(r->plain_ready){
        p.phase="Checking OBB";
        plain=hash_file(obb_path,acp_setup_plain_size,acp_setup_plain_hash,buffers,&p);
        if(plain==PAUSED)goto paused;
        p.base+=acp_setup_plain_size;
        if(!plain)p.total+=obb_bytes;
    }
    free(buffers);buffers=NULL;
    if(plain)r->reused++;
    else{
        if(!*r->obb)return fail(r,"The prepared OBB is damaged. Copy the original main.253 OBB into ux0:data/acpirates/ and press START again.");
        p.phase="Converting OBB";
        snprintf(temp,sizeof(temp),"%s.part",obb_path);
        rc=convert_obb(r->obb,temp,&p);
        if(rc==PAUSED)goto paused;
        if(rc==UNSUPPORTED)return fail(r,"This OBB is not supported. Use main.253 from Assassin's Creed Pirates 2.9.1.");
        if(rc==DAMAGED)return fail(r,"The OBB is incomplete or was damaged while copying. Copy main.253 of version 2.9.1 again.");
        if(rc==BROKEN_PACKAGE)return fail(r,"Setup data is missing from the game. Install ac-pirates.vpk again.");
        if(rc)return fail(r,"Converting the OBB failed. Check free space and the memory card, then try again.");
        /* The converted archive replaces the original (same name), or the
         * original under another name is removed once it is no longer needed.
         * On failure the verified .part stays: it is the only complete copy
         * if the original was already replaced. */
        if(publish(temp,obb_path))return fail(r,"Cannot replace the OBB. Check the memory card.");
        if(strcasecmp(r->obb,obb_path))remove(r->obb);
        r->plain_ready=1;r->obb[0]=0;r->completed++;
        p.base+=obb_bytes;
    }

    p.phase="Building startup cache";
    if(join(path,root,"startup.cache"))return fail(r,"The game folder path is too long.");
    buffers=malloc(CHUNK);if(!buffers)return fail(r,"Not enough memory for setup.");
    rc=hash_file(path,acp_setup_cache_size,acp_setup_cache_hash,buffers,&p);
    free(buffers);buffers=NULL;
    if(rc==PAUSED)goto paused;
    if(rc)r->reused++;
    else{
        uint8_t *blob=NULL;size_t size=0;
        rc=build_cache(obb_path,&blob,&size,&p);
        if(rc==PAUSED){free(blob);goto paused;}
        rc=rc||write_verified(path,blob,size,acp_setup_cache_hash)?-1:0;
        free(blob);
        if(rc)return fail(r,"Building the startup cache failed. Check free space, then try again.");
        r->completed++;
    }
    p.base+=acp_setup_cache_size;
    if(write_marker(root,apk_size,!pinned))return fail(r,"Cannot finish setup. Check the memory card.");
    report(&p,0);
    snprintf(r->message,sizeof(r->message),"Ready to play.");
    return 0;
paused:
    free(buffers);
    snprintf(r->message,sizeof(r->message),"Paused. Press START to continue.");
    return PAUSED;
}
