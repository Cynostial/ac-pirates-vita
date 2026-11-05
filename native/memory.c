/* Fixed backing: the game cannot consume the JNI/audio/OS reserve. MIT. */
#include "acp_native.h"
#include "acp_heap.h"
#include "memory_budget.h"
#include "acp_fast_mutex.h"
#include "tlsf/tlsf.h"
#include "acp_lua_vita.h"
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <limits.h>
#include <string.h>
#ifdef ACP_DEVELOPMENT_PROFILE
#include <stdio.h>
#endif
#define GAME_BYTES ACP_GAME_HEAP_BYTES
static void *arena,*space;
static acp_heap heap;
static struct {unsigned resize,freed,resize_samples,free_samples,resize_us,free_us,locks,waits;} stats;
static unsigned resize_tick,free_tick;
#ifdef ACP_GAME_HEAP_BASELINE
static SceKernelLwMutexWork mutex;
#else
static acp_fast_mutex mutex;
static SceUID semaphore=-1;
static int wait_heap(void *ctx){(void)ctx;return sceKernelWaitSema(semaphore,1,NULL);}
static int signal_heap(void *ctx){(void)ctx;return sceKernelSignalSema(semaphore,1);}
#endif
#ifdef ACP_DEVELOPMENT_PROFILE
extern int acp_import_begin(unsigned);
extern void acp_import_end(int);
#define BEGIN(i) int profile=acp_import_begin(i)
#define END() acp_import_end(profile)
#else
#define BEGIN(i) ((void)0)
#define END() ((void)0)
#endif
static void *resize(void *ctx,void *p,size_t bytes){
    BEGIN(4);stats.resize++;int sample=!(resize_tick++&255u);
    unsigned begin=sample?sceKernelGetProcessTimeLow():0;
#ifdef ACP_GAME_HEAP_BASELINE
    void *q=sceClibMspaceRealloc(ctx,p,bytes);
#else
    void *q=tlsf_realloc(ctx,p,bytes);
#endif
    if(sample){stats.resize_us+=sceKernelGetProcessTimeLow()-begin;stats.resize_samples++;}
    END();return q;
}
static void release(void *ctx,void *p){
    BEGIN(5);stats.freed++;int sample=!(free_tick++&255u);
    unsigned begin=sample?sceKernelGetProcessTimeLow():0;
#ifdef ACP_GAME_HEAP_BASELINE
    sceClibMspaceFree(ctx,p);
#else
    tlsf_free(ctx,p);
#endif
    if(sample){stats.free_us+=sceKernelGetProcessTimeLow()-begin;stats.free_samples++;}END();
}
static void lock(void){
#ifdef ACP_GAME_HEAP_BASELINE
    if(sceKernelLockLwMutex(&mutex,1,NULL)<0)acp_fatal("Game heap lock failed");
#else
    int waited=acp_fast_mutex_lock(&mutex,wait_heap,NULL);
    if(waited<0)acp_fatal("Game heap lock failed");
    stats.waits+=(unsigned)waited;
#endif
    stats.locks++;
}
static void unlock(void){
#ifdef ACP_GAME_HEAP_BASELINE
    if(sceKernelUnlockLwMutex(&mutex,1)<0)acp_fatal("Game heap unlock failed");
#else
    if(acp_fast_mutex_unlock(&mutex,signal_heap,NULL)<0)acp_fatal("Game heap unlock failed");
#endif
}
int acp_memory_init(void) {
    SceUID block=sceKernelAllocMemBlock("AC game heap",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,GAME_BYTES,NULL);
    if(block<0||sceKernelGetMemBlockBase(block,&arena)<0)return -1;
#ifdef ACP_GAME_HEAP_BASELINE
    space=sceClibMspaceCreate(arena,GAME_BYTES);
    int result=sceKernelCreateLwMutex(&mutex,"AC heap",0,0,NULL);
    acp_log("Native heap: mspace baseline, fixed %u MiB",GAME_BYTES/(1024*1024));
#else
    space=tlsf_create_with_pool(arena,GAME_BYTES);
    semaphore=sceKernelCreateSema("AC native heap",0,0,INT_MAX,NULL);
    int result=semaphore;
    acp_log("Native heap: TLSF, atomic fast path, fixed %u MiB",GAME_BYTES/(1024*1024));
#endif
    heap=(acp_heap){.context=space,.resize=resize,.release=release,.limit=GAME_BYTES};
    return space&&result>=0?0:-1;
}
/* After a temple run the sea reload's texture uploads asked for 16 MiB
 * buffers (image decode, vertical flip in geOesTexture::SetData, file read)
 * the fragmented arena could not place: std::bad_alloc closed the game (Vita:
 * 48 MiB free, largest piece 10 MiB, 20 MiB main RAM left). Such big blocks
 * live for milliseconds, so a request of at least LOAN_BYTES that still fails
 * is lent from the Lua arena while it keeps its reserve (acp_lua_vita_lend)
 * and returned on free. Last, up to ACP_GAME_HEAP_EXTENSIONS more pools are
 * added while main RAM stays above ACP_GAME_HEAP_FREE_FLOOR. */
#define LOAN_BYTES (1024u*1024u)
#define LOANS 8
static struct {void *p;size_t bytes;} loan[LOANS];
static struct {void *base;size_t bytes;SceUID block;} extension[ACP_GAME_HEAP_EXTENSIONS];
static unsigned extensions;
static struct {size_t request,used,free,largest,added,lent;int main_free;unsigned count;} failure;
/* Under the heap lock. */
static int loan_slot(void *p){for(int i=0;i<LOANS;i++)if(loan[i].p==p)return i;return -1;}
static int owned(void *p){
    if((uintptr_t)p>(uintptr_t)arena&&(uintptr_t)p-(uintptr_t)arena<GAME_BYTES)return 1;
    for(unsigned i=0;i<extensions;i++)
        if((uintptr_t)p>(uintptr_t)extension[i].base&&(uintptr_t)p-(uintptr_t)extension[i].base<extension[i].bytes)return 1;
    return 0;
}
#ifndef ACP_GAME_HEAP_BASELINE
static void free_block(void *ptr,size_t size,int used,void *user){
    (void)ptr;size_t *m=user;if(!used){m[0]+=size;if(size>m[1])m[1]=size;}
}
#endif
/* Under the heap lock: free-space figures of a failed request for
 * report_failure(). */
static void note_failure(size_t bytes){
    size_t m[2]={0,0};
#ifndef ACP_GAME_HEAP_BASELINE
    if(failure.count<32){   /* the walk visits every block: diagnostics only */
        tlsf_walk_pool(tlsf_get_pool(space),free_block,m);
        for(unsigned i=0;i<extensions;i++)tlsf_walk_pool(extension[i].base,free_block,m);
    }
#endif
    failure=(__typeof__(failure)){bytes,heap.used,m[0],m[1],0,0,-1,failure.count+1};
}
/* Under the heap lock, after note_failure(). */
static int extend(size_t bytes){
#ifdef ACP_GAME_HEAP_BASELINE
    return 0;
#else
    if(extensions>=ACP_GAME_HEAP_EXTENSIONS)return 0;
    /* TLSF's good-fit search rounds a request up by up to 1/32 of its size. */
    size_t need=bytes+bytes/16u+tlsf_pool_overhead()+tlsf_alloc_overhead()+4096u;
    size_t size=need<=ACP_GAME_HEAP_EXTENSION_BYTES?ACP_GAME_HEAP_EXTENSION_BYTES:(need+65535u)&~(size_t)65535u;
    SceKernelFreeMemorySizeInfo info={.size=sizeof(info)};
    if(sceKernelGetFreeMemorySize(&info)<0||info.size_user<0)return 0;
    failure.main_free=info.size_user;
    if((size_t)info.size_user<size+ACP_GAME_HEAP_FREE_FLOOR)return 0;
    SceUID block=sceKernelAllocMemBlock("AC game heap extension",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,size,NULL);
    void *base=NULL;
    if(block<0)return 0;
    if(sceKernelGetMemBlockBase(block,&base)<0||!tlsf_add_pool(space,base,size)){sceKernelFreeMemBlock(block);return 0;}
    extension[extensions++]=(__typeof__(extension[0])){base,size,block};
    heap.limit+=size;failure.added=size;
    return 1;
#endif
}
/* After the lock is released: the log writer must not run under it. */
static void report_failure(unsigned seen,int ok){
    if(failure.count==seen||failure.count>32)return;
    if(failure.lent)acp_log("Game heap: lent %u KiB from the Lua arena (largest Lua piece %u KiB); game used=%u KiB free=%u KiB largest-free=%u KiB",
        (unsigned)(failure.lent/1024),(unsigned)(acp_lua_vita_largest_free()/1024),
        (unsigned)(failure.used/1024),(unsigned)(failure.free/1024),(unsigned)(failure.largest/1024));
    else if(failure.added)acp_log("Game heap: extended by %u KiB for a %u KiB request (%u of %u extensions); used=%u KiB free=%u KiB largest-free=%u KiB main-free=%d KiB",
        (unsigned)(failure.added/1024),(unsigned)(failure.request/1024),extensions,ACP_GAME_HEAP_EXTENSIONS,
        (unsigned)(failure.used/1024),(unsigned)(failure.free/1024),(unsigned)(failure.largest/1024),failure.main_free/1024);
    else if(!ok)acp_log("Game heap: %u KiB request failed; used=%u KiB free=%u KiB largest-free=%u KiB main-free=%d KiB extensions=%u",
        (unsigned)(failure.request/1024),(unsigned)(failure.used/1024),(unsigned)(failure.free/1024),
        (unsigned)(failure.largest/1024),failure.main_free/1024,extensions);
}
#ifdef ACP_DEVELOPMENT_PROFILE
/* Profile only: who holds the big blocks. libSparkApp return addresses found
 * on our stack (it is mapped at 0x98000000; emulator symbolization). */
#define BIG_BYTES (1024u*1024u)
static void big_event(const char *what,void *p,size_t bytes){
    uint32_t marker;const uint32_t *sp=&marker;char frames[96];int n=0,found=0;
    for(int i=0;i<160&&found<5;i++){
        uint32_t w=sp[i];
        if(w>=0x98000000u&&w<0x99400000u){n+=snprintf(frames+n,sizeof(frames)-n," %x",(unsigned)(w-0x98000000u));found++;}
    }
    frames[n]=0;
    acp_log("Game heap big %s %p %u KiB live=%u KiB so:%s",what,p,(unsigned)(bytes/1024),(unsigned)(heap.used/1024),frames);
}
#else
#define big_event(what,p,bytes) ((void)0)
#endif
void *acp_memalign(size_t alignment,size_t bytes) {
    if(alignment<8)alignment=8;
    if(!space || (alignment&(alignment-1)) || bytes>GAME_BYTES){acp_error(12);return NULL;}
    lock();void *p=acp_heap_alloc(&heap,alignment,bytes);
    unsigned seen=failure.count;
    if(!p)note_failure(bytes);
    if(!p&&bytes<LOAN_BYTES&&extend(bytes+alignment))p=acp_heap_alloc(&heap,alignment,bytes);
    unlock();
    int loaned=0;
    if(!p&&bytes>=LOAN_BYTES){
        /* The Lua lock is taken after the game heap lock is released. */
        void *lent=acp_lua_vita_lend(alignment,bytes);
        lock();
        int slot=lent?loan_slot(NULL):-1;
        if(slot>=0){loan[slot].p=lent;loan[slot].bytes=bytes;failure.lent=bytes;p=lent;loaned=1;}
        if(!p)p=acp_heap_alloc(&heap,alignment,bytes);
        if(!p&&extend(bytes+alignment))p=acp_heap_alloc(&heap,alignment,bytes);
        unlock();
        if(lent&&slot<0)acp_lua_vita_reclaim(lent);
    }
    report_failure(seen,p!=NULL);if(!p)acp_error(12);
    (void)loaned;
#ifdef ACP_DEVELOPMENT_PROFILE
    if(bytes>=BIG_BYTES)big_event(loaned?"loan":p?"alloc":"fail",p,bytes);
#endif
    return p;
}
void *acp_malloc(size_t bytes){return acp_memalign(16,bytes);}
void *acp_calloc(size_t n,size_t size){
    if(size&&n>SIZE_MAX/size){acp_error(12);return NULL;}
    void *p=acp_malloc(n*size);if(p)memset(p,0,n*size);return p;
}
void acp_free(void *p){
    if(!p)return;
    lock();int slot=loan_slot(p);size_t lent=slot>=0?loan[slot].bytes:0;if(slot>=0)loan[slot].p=NULL;unlock();
    if(slot>=0){
        acp_lua_vita_reclaim(p);(void)lent;
#ifdef ACP_DEVELOPMENT_PROFILE
        big_event("return",p,lent);
#endif
        return;
    }
    if(!owned(p))acp_fatal("Game free outside its heap: %p",p);
#ifdef ACP_DEVELOPMENT_PROFILE
    size_t size=acp_usable(p);
#endif
    lock();int result=acp_heap_free(&heap,p);unlock();
    if(result)acp_fatal("Invalid game allocation header: %p",p);
#ifdef ACP_DEVELOPMENT_PROFILE
    if(size>=BIG_BYTES)big_event("free",p,size);
#endif
}
void *acp_realloc(void *p,size_t bytes){
    if(!p)return acp_malloc(bytes);
    if(!bytes){acp_free(p);return NULL;}
    lock();int slot=loan_slot(p);size_t lent=slot>=0?loan[slot].bytes:0;unlock();
    if(slot>=0){
        void *next=acp_memalign(16,bytes);
        if(next){memcpy(next,p,lent<bytes?lent:bytes);acp_free(p);}
        return next;
    }
    if(!owned(p))acp_fatal("Game realloc outside its heap: %p",p);
    if(bytes>GAME_BYTES){acp_error(12);return NULL;}
    lock();void *next=acp_heap_resize(&heap,p,bytes);
    unsigned seen=failure.count;
    if(!next)note_failure(bytes);
    if(!next&&bytes<LOAN_BYTES&&extend(bytes+16))next=acp_heap_resize(&heap,p,bytes);
    size_t old=next?0:acp_heap_size(&heap,p);
    unlock();report_failure(seen,next!=NULL||bytes>=LOAN_BYTES);
    if(!next&&bytes>=LOAN_BYTES){
        /* A big block that cannot grow in place moves: acp_memalign may lend. */
        next=acp_memalign(16,bytes);
        if(next){memcpy(next,p,old<bytes?old:bytes);acp_free(p);}
    }
    if(!next)acp_error(12);
#ifdef ACP_DEVELOPMENT_PROFILE
    if(bytes>=BIG_BYTES)big_event(next?"realloc":"fail-realloc",next,bytes);
#endif
    return next;
}
int acp_memory_writable(const void *p,size_t n){
    uintptr_t a=(uintptr_t)p;
    if(a>(uintptr_t)arena&&a-(uintptr_t)arena<GAME_BYTES&&n<=GAME_BYTES-(a-(uintptr_t)arena))return 1;
    int ok=0;
    lock();
    for(unsigned i=0;i<extensions&&!ok;i++){
        uintptr_t b=(uintptr_t)extension[i].base;
        ok=a>b&&a-b<extension[i].bytes&&n<=extension[i].bytes-(a-b);
    }
    for(int i=0;i<LOANS&&!ok;i++){
        uintptr_t b=(uintptr_t)loan[i].p;
        ok=loan[i].p&&a>=b&&a-b<loan[i].bytes&&n<=loan[i].bytes-(a-b);
    }
    unlock();return ok;
}
size_t acp_usable(void *p){
    if(!p)return 0;
    lock();int slot=loan_slot(p);size_t n=slot>=0?loan[slot].bytes:owned(p)?acp_heap_size(&heap,p):0;unlock();return n;
}
char *acp_strdup(const char *text){size_t n=strlen(text)+1;char *p=acp_malloc(n);if(p)memcpy(p,text,n);return p;}
void acp_memory_usage(size_t *out,size_t *high){lock();*out=heap.used;*high=heap.peak;unlock();}
void acp_native_heap_report(void){
    lock();__typeof__(stats) s=stats;memset(&stats,0,sizeof(stats));
    size_t used=heap.used,peak=heap.peak;unsigned failures=heap.failures;unlock();
    acp_log("Native heap (1/256): resize=%u sample=%u us n=%u free=%u sample=%u us n=%u entries=%u waits=%u live=%u KiB peak=%u KiB failures=%u",
        s.resize,s.resize_us,s.resize_samples,s.freed,s.free_us,s.free_samples,s.locks,s.waits,
        (unsigned)(used/1024),(unsigned)(peak/1024),failures);
}
