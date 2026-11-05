#include "acp_native.h"
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "acp_log_queue.h"
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static acp_log_queue queue;
static int asynchronous,io_busy;
static void write_log(const char *text,size_t n){
    int fd=sceIoOpen("ux0:data/acpirates/runtime.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
    if(fd>=0){
        size_t offset=0;
        while(offset<n){int written=sceIoWrite(fd,text+offset,n-offset);if(written<=0)break;offset+=(unsigned)written;}
        sceIoClose(fd);
    }
    sceClibPrintf("%.*s",(int)n,text);
}
static int log_worker(SceSize bytes,void *args){
    (void)bytes;(void)args;char batch[8192];
    for(;;){
        pthread_mutex_lock(&mutex);
        size_t n=acp_log_queue_pop(&queue,batch,sizeof(batch));
        unsigned lost=queue.dropped;queue.dropped=0;io_busy=!!(n||lost);
        pthread_mutex_unlock(&mutex);
        if(n)write_log(batch,n);
        if(lost){char message[96];int len=snprintf(message,sizeof(message),"\nLog queue full: %u records dropped\n",lost);write_log(message,(size_t)len);}
        pthread_mutex_lock(&mutex);io_busy=0;pthread_mutex_unlock(&mutex);
        if(!n)sceKernelDelayThread(50000);
    }
    return 0;
}
void acp_log_init(void){
    SceUID thread=sceKernelCreateThread("ACP log writer",log_worker,0x10000110,0x10000,0,0,NULL);
    if(thread>=0){
        if(sceKernelStartThread(thread,0,NULL)>=0)__atomic_store_n(&asynchronous,1,__ATOMIC_RELEASE);
        else sceKernelDeleteThread(thread);
    }
    acp_log("Log writer: %s",__atomic_load_n(&asynchronous,__ATOMIC_ACQUIRE)?"bounded 64 KiB queue, background storage writes":"synchronous fallback");
}
void acp_log(const char *fmt,...) {
    char text[2048];
    unsigned long long ms=sceKernelGetProcessTimeWide()/1000;
    int prefix=snprintf(text,sizeof(text),"[%llu.%03u] ",ms/1000,(unsigned)(ms%1000));
    if(prefix<0||prefix>=(int)sizeof(text)-3)return;
    va_list a;va_start(a,fmt);
    int n=vsnprintf(text+prefix,sizeof(text)-prefix-2,fmt,a);va_end(a);
    if(n<0)return;
    if(n>(int)sizeof(text)-prefix-3)n=sizeof(text)-prefix-3;
    n+=prefix;
    text[n++]='\n';text[n]=0;
    pthread_mutex_lock(&mutex);
    if(__atomic_load_n(&asynchronous,__ATOMIC_ACQUIRE))acp_log_queue_push(&queue,text,(size_t)n);
    else write_log(text,(size_t)n);
    pthread_mutex_unlock(&mutex);
}
void acp_log_flush(void){
    if(!__atomic_load_n(&asynchronous,__ATOMIC_ACQUIRE))return;
    uint64_t deadline=sceKernelGetProcessTimeWide()+2000000;
    do{
        pthread_mutex_lock(&mutex);int pending=queue.used||queue.dropped||io_busy;pthread_mutex_unlock(&mutex);
        if(!pending)return;
        sceKernelDelayThread(1000);
    }while(sceKernelGetProcessTimeWide()<deadline);
}
void acp_fatal(const char *fmt,...) {
    char text[1024];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);
    acp_log("STOP: %s",text);acp_log_flush();sceKernelExitProcess(1);
    __builtin_unreachable();
}
void fndk_log(int severity,const char *message){acp_log("NDK %d: %s",severity,message);}

/* Preserve native-library failures as well as Android-side failures. Newlib's
 * stderr is not connected to the Vita log, so assertions otherwise disappear. */
extern void __real_abort(void) __attribute__((noreturn));
void __wrap_abort(void){
    const unsigned *stack;
    __asm__ volatile("mov %0, sp":"=r"(stack));
    acp_log("Native abort caller=%p sp=%p",__builtin_return_address(0),stack);
    for(unsigned i=0;i<64;i+=8)
        acp_log("Native stack %02u: %08x %08x %08x %08x %08x %08x %08x %08x",i,
                stack[i],stack[i+1],stack[i+2],stack[i+3],stack[i+4],stack[i+5],stack[i+6],stack[i+7]);
    acp_log_flush();__real_abort();
}
extern void __real___assert_func(const char*,int,const char*,const char*) __attribute__((noreturn));
void __wrap___assert_func(const char *file,int line,const char *function,const char *expression){
    acp_log("Native assertion %s:%d %s: %s",file,line,function?function:"",expression);
    acp_log_flush();__real___assert_func(file,line,function,expression);
}
