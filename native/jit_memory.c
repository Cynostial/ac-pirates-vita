/* LuaJIT ARM branches need code within reach of the game's VM. Keep the
 * executable allocation separate from both data heaps, bounded to 4 MiB. */
#include "acp_native.h"
#include <kubridge.h>
#include <psp2/kernel/sysmem.h>
#include <pthread.h>
#include <string.h>
#define BASE 0x97b00000u
#define PAGE 4096u
#define PAGES 1024u
static SceUID block=-1;
static unsigned short spans[PAGES];
static unsigned used,peak,flushes;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
int acp_jit_init(void){
    SceKernelAllocMemBlockKernelOpt opt={0};opt.size=sizeof(opt);opt.attr=1;opt.field_C=BASE;
    block=kuKernelAllocMemBlock("ACP LuaJIT code",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,PAGES*PAGE,&opt);
    void *address=NULL;
    if(block<0||sceKernelGetMemBlockBase(block,&address)<0||address!=(void *)BASE){
        if(block>=0)sceKernelFreeMemBlock(block);
        block=-1;
        acp_log("LuaJIT code reservation unavailable; using interpreter");return 0;
    }
    acp_log("LuaJIT: bounded 4096 KiB code reservation at %p",address);return 1;
}
int acp_jit_contains(const void *p){return block>=0&&(uintptr_t)p>=BASE&&(uintptr_t)p<BASE+PAGES*PAGE;}
int acp_jit_hint(const void *p){return block>=0&&(uintptr_t)p>=0x96000000u&&(uintptr_t)p<0x9a800000u;}
static unsigned protection(int p){return ((p&1)?KU_KERNEL_PROT_READ:0)|((p&2)?KU_KERNEL_PROT_WRITE:0)|((p&4)?KU_KERNEL_PROT_EXEC:0);}
void *acp_jit_map(size_t bytes,int prot){
    if(block<0||!bytes||bytes>PAGES*PAGE||prot<0||prot>7||(prot&6)==6)return (void *)-1;
    unsigned n=(bytes+PAGE-1)/PAGE;void *result=(void *)-1;
    pthread_mutex_lock(&mutex);
    for(unsigned i=0;i+n<=PAGES;i++){
        unsigned j=0;while(j<n&&!spans[i+j])j++;
        if(j<n){i+=j;continue;}
        void *p=(void *)(BASE+i*PAGE);
        if(kuKernelMemProtect(p,n*PAGE,KU_KERNEL_PROT_READ|KU_KERNEL_PROT_WRITE)<0)break;
        memset(p,0,n*PAGE);
        if(kuKernelMemProtect(p,n*PAGE,protection(prot))<0)break;
        spans[i]=(unsigned short)n;for(j=1;j<n;j++)spans[i+j]=0xffff;
        used+=n;if(used>peak)peak=used;result=p;break;
    }
    pthread_mutex_unlock(&mutex);return result;
}
int acp_jit_unmap(void *p,size_t bytes){
    uintptr_t offset=(uintptr_t)p-BASE;
    if(!acp_jit_contains(p)||(offset&(PAGE-1))||!bytes||bytes>PAGES*PAGE)return -1;
    unsigned i=offset/PAGE,n=(bytes+PAGE-1)/PAGE;int result=-1;
    pthread_mutex_lock(&mutex);
    if(spans[i]==n){
        if(kuKernelMemProtect(p,n*PAGE,KU_KERNEL_PROT_READ|KU_KERNEL_PROT_WRITE)>=0){
            memset(spans+i,0,n*sizeof(*spans));used-=n;result=0;
        }
    }
    pthread_mutex_unlock(&mutex);return result;
}
int acp_jit_protect(void *p,size_t bytes,int prot){
    uintptr_t offset=(uintptr_t)p-BASE;
    if(!acp_jit_contains(p)||(offset&(PAGE-1))||!bytes||bytes>PAGES*PAGE-offset||prot<0||prot>7||(prot&6)==6)return -1;
    unsigned i=offset/PAGE,n=(bytes+PAGE-1)/PAGE;int result=-1;
    pthread_mutex_lock(&mutex);
    if(spans[i]==n)result=kuKernelMemProtect(p,n*PAGE,protection(prot))<0?-1:0;
    pthread_mutex_unlock(&mutex);return result;
}
void acp_jit_clear_cache(void *start,void *end){
    if((uintptr_t)end<=(uintptr_t)start)return;
    kuKernelFlushCaches(start,(uintptr_t)end-(uintptr_t)start);
    unsigned count=__atomic_add_fetch(&flushes,1,__ATOMIC_RELAXED);
    if(count<=3||!(count%512))acp_log("LuaJIT: code flushes=%u code=%u KiB peak=%u KiB",count,used*4,peak*4);
}
