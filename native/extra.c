#define _GNU_SOURCE
#include "acp_native.h"
#include "memory_budget.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <libgen.h>
#include <zlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <psp2/kernel/threadmgr.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
static int unsupported(void){return acp_error(38);}
static int offline(void){return acp_error(100);}
static int dns(const char *host,const char *service,const void *hints,void **out){
    (void)host;(void)service;(void)hints;if(out)*out=NULL;return -2;
}
static void free_dns(void *p){if(p)acp_fatal("Unexpected network address object");}
static int identity(void){return 10000;}
static void *password(unsigned uid){(void)uid;acp_error(2);return NULL;}
static void *no_process(const char *cmd,const char *mode){(void)cmd;(void)mode;acp_error(38);return NULL;}
static unsigned alarm_failed(unsigned seconds){(void)seconds;acp_error(38);return 0;}
static int tid(void){return sceKernelGetThreadId();}
static int android_finitef(float f){return isfinite(f);}
static void region(void){} /* Optional Android instrumentation, no state or work. */
static int log_v(int priority,const char *tag,const char *fmt,va_list a){
    (void)priority;char text[1536];int r=vsnprintf(text,sizeof(text),fmt,a);acp_log("%s: %s",tag?tag:"Android",text);return r;
}
static void log_assert(const char *condition,const char *tag,const char *fmt,...){
    char text[1024]="";if(fmt){va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);}
    acp_fatal("%s assertion %s: %s",tag?tag:"Android",condition?condition:"",text);
}
static void assertion(const char *file,int line,const char *expression){acp_fatal("%s:%d %s",file,line,expression);}
static unsigned saved_umask=0022;
static unsigned mask(unsigned n){unsigned old=saved_umask;saved_umask=n&0777;return old;}
static char *signal_name(int sig){(void)sig;return "Unsupported POSIX signal";}
static int kill_process(int pid,int sig){(void)pid;if(!sig)return 0;return acp_error(38);}
static char *get_name(unsigned index,char *buf){(void)index;(void)buf;acp_error(19);return NULL;}
typedef struct {int arena,ordblks,smblks,hblks,hblkhd,usmblks,fsmblks,uordblks,fordblks,keepcost;} BionicMallinfo;
static BionicMallinfo info(void){size_t used,high;acp_memory_usage(&used,&high);BionicMallinfo r={0};r.arena=ACP_GAME_HEAP_BYTES;r.uordblks=used;r.usmblks=high;r.fordblks=r.arena-used;return r;}
static int sem_init_android(sem_t *s,int shared,unsigned value){if(shared)return acp_error(38);int r=sem_init(s,0,value);if(r)acp_error(acp_errno_code(errno));return r;}
static int sem_wait_android(sem_t *s){int r=sem_wait(s);if(r)acp_error(acp_errno_code(errno));return r;}
static int sem_try_android(sem_t *s){int r=sem_trywait(s);if(r)acp_error(acp_errno_code(errno));return r;}
static int sem_post_android(sem_t *s){int r=sem_post(s);if(r)acp_error(acp_errno_code(errno));return r;}
static int pthread_kill_android(pthread_t t,int sig){(void)t;return sig?38:0;}
typedef struct {void *iov_base;size_t iov_len;} BionicIovec;
static int write_vector(int fd,const BionicIovec *vectors,int count){
    extern int fndk_write(int,const void *,size_t);
    if(count<0||count>1024)return acp_error(22);
    size_t done=0;for(int i=0;i<count;i++){int n=fndk_write(fd,vectors[i].iov_base,vectors[i].iov_len);
        if(n<0)return done?(int)done:acp_error(acp_errno_code(errno));
        done+=n;if((size_t)n!=vectors[i].iov_len)break;}return done;
}
static int fcntl_android(int fd,int command,...){
    va_list a;va_start(a,command);int value=0;
    if(command==2||command==4)value=va_arg(a,int);va_end(a);
    /* The virtual NativeActivity pipe has no descriptor flags to inherit. */
    if(fd>=384&&fd<448){if(command==1||command==2)return 0;if(command==3)return 0;return acp_error(95);}
    int native=command==1?F_GETFD:command==2?F_SETFD:command==3?F_GETFL:command==4?F_SETFL:-1;
    if(native<0)return acp_error(38);
    if(command==4)value=(value&04000?O_NONBLOCK:0)|(value&02000?O_APPEND:0);
    int r=fcntl(fd,native,value);if(r<0)acp_error(acp_errno_code(errno));return r;
}
int _opensles_user_freq=44100;
#define B(n,f) {n,(uintptr_t)&f}
uintptr_t acp_extra_resolve(const char *name){
    static const AcpSymbol symbols[]={
        B("__android_log_assert",log_assert),B("__android_log_vprint",log_v),B("__assert",assertion),
        B("__google_potentially_blocking_region_begin",region),B("__google_potentially_blocking_region_end",region),
        B("__isfinitef",android_finitef),B("atanf",atanf),B("atoll",atoll),B("basename",basename),B("btowc",btowc),
        B("compress",compress),B("uncompress",uncompress),B("difftime",difftime),
        B("dlmalloc_usable_size",acp_usable),B("memalign",acp_memalign),B("strdup",acp_strdup),B("mallinfo",info),
        B("fcntl",fcntl_android),B("fchmod",fchmod),B("frexp",frexp),B("iswctype",iswctype),B("isxdigit",isxdigit),
        B("getegid",identity),B("geteuid",identity),B("getgid",identity),B("getuid",identity),B("gettid",tid),B("getpwuid",password),
        B("log10f",log10f),B("lrand48",lrand48),B("srand48",srand48),B("mbstowcs",mbstowcs),
        B("pthread_kill",pthread_kill_android),B("sem_init",sem_init_android),B("sem_post",sem_post_android),
        B("sem_wait",sem_wait_android),B("sem_trywait",sem_try_android),B("sem_close",unsupported),
        B("sigsetjmp",setjmp),B("siglongjmp",longjmp),B("sleep",sleep),B("strcspn",strcspn),
        B("strsignal",signal_name),B("strxfrm",strxfrm),B("trunc",trunc),B("umask",mask),
        B("vprintf",vprintf),B("wcsxfrm",wcsxfrm),B("wctob",wctob),B("wctype",wctype),B("wmemchr",wmemchr),
        B("writev",write_vector),B("popen",no_process),B("pclose",unsupported),B("syscall",unsupported),
        B("alarm",alarm_failed),B("bsd_signal",unsupported),B("sigaction",unsupported),B("sigaltstack",unsupported),
        B("setitimer",unsupported),B("kill",kill_process),
        B("socket",offline),B("accept",offline),B("bind",offline),B("connect",offline),B("listen",offline),
        B("recv",offline),B("recvfrom",offline),B("send",offline),B("sendto",offline),B("shutdown",offline),
        B("getpeername",offline),B("getsockname",offline),B("getsockopt",offline),B("setsockopt",offline),
        B("poll",offline),B("getaddrinfo",dns),B("freeaddrinfo",free_dns),B("getnameinfo",offline),
        B("if_indextoname",get_name),B("inet_addr",inet_addr),B("inet_ntoa",inet_ntoa),B("inet_pton",inet_pton),
        B("slCreateEngine",slCreateEngine),B("SL_IID_ENGINE",SL_IID_ENGINE),B("SL_IID_PLAY",SL_IID_PLAY),
        B("SL_IID_VOLUME",SL_IID_VOLUME),B("SL_IID_BUFFERQUEUE",SL_IID_BUFFERQUEUE),
        B("SL_IID_ANDROIDSIMPLEBUFFERQUEUE",SL_IID_ANDROIDSIMPLEBUFFERQUEUE),
    };
    for(size_t i=0;i<sizeof(symbols)/sizeof(*symbols);i++)if(!strcmp(name,symbols[i].name))return symbols[i].address;
    return 0;
}
