/* Android process/time/memory services implemented over VitaSDK. MIT. */
#define _GNU_SOURCE
#include <vitasdk.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <locale.h>
#include <wchar.h>
#include <setjmp.h>
#include <fnmatch.h>
#include <zlib.h>
#include "acp_native.h"

static __thread int android_errno;
static uintptr_t stack_guard=0x7194c25b;

int acp_errno_code(int e) {
    if (e==EAGAIN) return 11;
    if (e==ENOSYS) return 38;
    if (e==EOVERFLOW) return 75;
    if (e==ETIMEDOUT) return 110;
    if (e==EILSEQ) return 84;
    if (e==ENOTSUP) return 95;
    if (e==ECANCELED) return 125;
    if (e==EDEADLK) return 35;
    if (e==ENAMETOOLONG) return 36;
    if (e==ENOLCK) return 37;
    if (e==ENOTEMPTY) return 39;
    if (e==ELOOP) return 40;
    return e;
}
int *acp_errno_location(void) { return &android_errno; }
int acp_error(int e) { android_errno=e; return -1; }
static char *jstrerror(int e) {
    if (e==35) return "Resource deadlock avoided";
    if (e==36) return "File name too long";
    if (e==37) return "No locks available";
    if (e==39) return "Directory not empty";
    if (e==40) return "Too many symbolic links";
    if (e==38) return "Function not implemented";
    if (e==75) return "Value too large";
    if (e==110) return "Operation timed out";
    if (e==84) return "Invalid multibyte sequence";
    if (e==95) return "Operation not supported";
    return strerror(e);
}
static int jstrerror_r(int e,char *buf,size_t n) {
    if (!buf || !n) return 34;
    const char *text=jstrerror(e); size_t len=strlen(text);
    snprintf(buf,n,"%s",text); return len>=n?34:0;
}
static void jperror(const char *s) { acp_log("%s: %s",s?s:"",jstrerror(android_errno)); }
static int jlog_print(int priority,const char *tag,const char *fmt,...) {
    (void)priority; char text[1536]; va_list a; va_start(a,fmt); int r=vsnprintf(text,sizeof(text),fmt,a); va_end(a);
    acp_log("%s: %s",tag?tag:"Android",text); return r;
}
static int jlog_write(int priority,const char *tag,const char *text) { return jlog_print(priority,tag,"%s",text?text:""); }
static void game_stdout(const char *text) {
    static unsigned messages;
    static int verbose=-1;
    if(verbose<0){FILE *f=fopen(ACP_DATA_PATH "/verbose-game.flag","rb");verbose=f!=NULL;if(f)fclose(f);}
    unsigned ticket=__atomic_fetch_add(&messages,1,__ATOMIC_RELAXED);
    if((verbose&&ticket<20000)||ticket<64 || strstr(text,"bad_alloc") || strstr(text,"terminating with") || strstr(text,"uncaught exception"))acp_log("%s",text);
    else if(ticket==64)acp_log("Further game stdout omitted from disk log; runtime and error reports remain enabled.");
}
static int jprintf(const char *fmt,...) {
    char text[2048];va_list a;va_start(a,fmt);int length=vsnprintf(text,sizeof(text),fmt,a);va_end(a);
    if(length>=0)game_stdout(text);
    return length;
}
static int jputs(const char *s) { game_stdout(s);return 0; }
static int jputchar(int c) { char text[2]={(char)c,0};game_stdout(text);return (unsigned char)c; }
static void jabort(void) { acp_fatal("The game called abort() from %08x.",(unsigned)(uintptr_t)__builtin_return_address(0)); }
static void jassert(const char *f,int line,const char *fn,const char *expr) { acp_fatal("Assertion %s:%d (%s): %s",f,line,fn,expr); }
static void jstackfail(void) { acp_fatal("Game stack protector failed."); }
static void jexit(int value) { acp_log("Game exited with status %d",value); acp_log_flush();sceKernelExitProcess(value); }

int acp_clock_gettime(int id,AcpTimespec *out) {
    if (!out) return acp_error(14);
    uint64_t micros;
    if (id==0 || id==5) { struct timeval t; gettimeofday(&t,NULL); micros=(uint64_t)t.tv_sec*1000000+t.tv_usec; }
    else if (id==1 || id==2 || id==3 || id==4 || id==6 || id==7) micros=sceKernelGetProcessTimeWide();
    else return acp_error(22);
    out->sec=(int32_t)(micros/1000000); out->nsec=(micros%1000000)*1000; return 0;
}
static int jgettimeofday(AcpTimeval *out,void *zone) {
    (void)zone; if (!out) return acp_error(14);
    struct timeval t; int r=gettimeofday(&t,NULL); if (r) return acp_error(acp_errno_code(errno));
    out->sec=t.tv_sec; out->usec=t.tv_usec; return 0;
}
static int32_t jtime(int32_t *out) { time_t t=time(NULL); if (out) *out=(int32_t)t; return (int32_t)t; }
static int32_t jclock(void) { return (int32_t)sceKernelGetProcessTimeWide(); }
static int jinflate(z_streamp s,int f) { return inflate(s,f); }
static int jnanosleep(const AcpTimespec *t,AcpTimespec *remaining) {
    if (!t || t->sec<0 || t->nsec<0 || t->nsec>=1000000000) return acp_error(22);
    uint64_t us=(uint64_t)t->sec*1000000+(t->nsec+999)/1000;
    while (us) { unsigned chunk=us>1000000000?1000000000:(unsigned)us; sceKernelDelayThread(chunk); us-=chunk; }
    if (remaining) *remaining=(AcpTimespec){0,0};
    return 0;
}
typedef struct { int value[9]; int32_t gmtoff; const char *zone; } BionicTM;
static __thread BionicTM time_buffer;
static void tm_out(BionicTM *out,const struct tm *in) { memcpy(out->value,in,36); out->gmtoff=0; out->zone="UTC"; }
static void tm_in(struct tm *out,const BionicTM *in) { memset(out,0,sizeof(*out)); memcpy(out,in->value,36); }
static BionicTM *jgmtime_r(const int32_t *t,BionicTM *out) { if (!t||!out) return NULL; time_t n=*t; struct tm v; if (!gmtime_r(&n,&v)) return NULL; tm_out(out,&v); return out; }
static BionicTM *jlocaltime_r(const int32_t *t,BionicTM *out) { return jgmtime_r(t,out); } /* Saves use a consistent UTC clock. */
static BionicTM *jgmtime(const int32_t *t) { return jgmtime_r(t,&time_buffer); }
static BionicTM *jlocaltime(const int32_t *t) { return jlocaltime_r(t,&time_buffer); }
static int32_t jmktime(BionicTM *t) { struct tm n; tm_in(&n,t); time_t r=mktime(&n); tm_out(t,&n); return (int32_t)r; }
static char *jasctime(const BionicTM *t) { struct tm n; tm_in(&n,t); return asctime(&n); }
static char *jctime(const int32_t *t) { return jasctime(jgmtime(t)); }
static size_t jstrftime(char *out,size_t size,const char *fmt,const BionicTM *t) { struct tm n; tm_in(&n,t); return strftime(out,size,fmt,&n); }
static size_t jwcsftime(wchar_t *out,size_t size,const wchar_t *fmt,const BionicTM *t){struct tm n;tm_in(&n,t);return wcsftime(out,size,fmt,&n);}

typedef struct Mapping { struct Mapping *next; void *address; size_t size; } Mapping;
static Mapping *mappings;
static pthread_mutex_t map_lock=PTHREAD_MUTEX_INITIALIZER;
extern int acp_jit_contains(const void *),acp_jit_hint(const void *);
extern void *acp_jit_map(size_t,int);
extern int acp_jit_unmap(void *,size_t),acp_jit_protect(void *,size_t,int);
static void *jmmap(void *hint,size_t bytes,int prot,int flags,int fd,int32_t offset) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0)&~1u;
    if((flags&0x22)==0x22&&!(flags&0x10)&&fd==-1&&offset==0&&
       (acp_jit_hint(hint)||caller==0x986dbd14u)){
        void *p=acp_jit_map(bytes,prot);if(p==(void *)-1)acp_error(12);return p;
    }
    if (!bytes || bytes>SIZE_MAX-4095 || (flags&0x10) || (prot&4) || prot!=3) { acp_error(22); return (void*)-1; }
    void *memory=acp_memalign(4096,(bytes+4095)&~4095u);
    if (!memory) { acp_error(12); return (void*)-1; }
    memset(memory,0,bytes);
    if (!(flags&0x20)) {
        if (!(flags&2) || offset<0) { acp_free(memory); acp_error(95); return (void*)-1; }
        ssize_t n=pread(fd,memory,bytes,offset);
        if (n<0) { acp_free(memory); acp_error(acp_errno_code(errno)); return (void*)-1; }
    }
    Mapping *m=malloc(sizeof(*m)); if (!m) { acp_free(memory); acp_error(12); return (void*)-1; }
    *m=(Mapping){NULL,memory,bytes}; pthread_mutex_lock(&map_lock); m->next=mappings; mappings=m; pthread_mutex_unlock(&map_lock);
    return memory;
}
static int jmunmap(void *ptr,size_t bytes) {
    if(acp_jit_contains(ptr))return acp_jit_unmap(ptr,bytes)?acp_error(22):0;
    pthread_mutex_lock(&map_lock); Mapping **link=&mappings;
    while (*link && (*link)->address!=ptr) link=&(*link)->next;
    if (!*link || bytes!=(*link)->size) { pthread_mutex_unlock(&map_lock); return acp_error(22); }
    Mapping *m=*link; *link=m->next; pthread_mutex_unlock(&map_lock); acp_free(m->address); free(m); return 0;
}
static int jmprotect(void *ptr,size_t bytes,int prot) {
    if(acp_jit_contains(ptr))return acp_jit_protect(ptr,bytes,prot)?acp_error(22):0;
    /* These heap-backed mappings are read/write. Other protection requests
     * cannot be applied safely to allocator pages and must report failure. */
    if (prot!=3) return acp_error(95);
    pthread_mutex_lock(&map_lock); int found=0;
    for (Mapping *m=mappings;m;m=m->next) if ((uintptr_t)ptr>=(uintptr_t)m->address && bytes<=m->size && (uintptr_t)ptr-(uintptr_t)m->address<=m->size-bytes) found=1;
    pthread_mutex_unlock(&map_lock); return found?0:acp_error(22);
}
static void *jmremap(void *ptr,size_t old,size_t size,int flags,...) {
    if (!(flags&1) || (flags&2)) { acp_error(95); return (void*)-1; }
    pthread_mutex_lock(&map_lock); int found=0;
    for (Mapping *m=mappings;m;m=m->next) if (m->address==ptr && m->size==old) found=1;
    pthread_mutex_unlock(&map_lock);
    if (!found) { acp_error(22); return (void*)-1; }
    void *newptr=jmmap(NULL,size,3,0x22,-1,0); if (newptr==(void*)-1) return newptr;
    memcpy(newptr,ptr,size<old?size:old); jmunmap(ptr,old); return newptr;
}
static int jenosys(void) { return acp_error(38); }
static int jioctl(int fd,int request,...) { (void)fd;(void)request;return acp_error(25); }
static int jgetpid(void) { return sceKernelGetProcessId(); }
static int jhostname(char *buf,size_t n) { if (n<7) return acp_error(36); memcpy(buf,"PSVita",7); return 0; }
static long jsysconf(int name) {
    switch(name) { case 0x27: case 0x28: return 4096; case 0x60:case 0x61:return 3; case 0x62:return 512*256; case 0x63:{ SceKernelFreeMemorySizeInfo m={.size=sizeof(m)};sceKernelGetFreeMemorySize(&m);return m.size_user/4096;} case 0x06:return 100; case 0x0b:return 64; default:return acp_error(22); }
}
static int jsysinfo(uint32_t *p) { if (!p) return acp_error(14); memset(p,0,64); SceKernelFreeMemorySizeInfo m={.size=sizeof(m)};sceKernelGetFreeMemorySize(&m);p[0]=sceKernelGetProcessTimeWide()/1000000;p[4]=512*1024*1024;p[5]=m.size_user;p[10]=1;p[13]=1;return 0; }
static int jgetrlimit(int resource,uint32_t *out) { if (!out) return acp_error(14);out[0]=out[1]=resource==7?64:resource==3?1024*1024:0xffffffffu;return 0; }
static int jsetrlimit(int resource,const uint32_t *in) { uint32_t current[2];jgetrlimit(resource,current);if(!in)return acp_error(14);return in[0]==current[0]&&in[1]==current[1]?0:acp_error(1); }
static int jsetpriority(int which,int who,int priority) { (void)which;(void)who;(void)priority;return acp_error(13); }
static int32_t jtimes(int32_t *p) { if(p){p[0]=sceKernelGetProcessTimeWide()/10000;p[1]=p[2]=p[3]=0;}return sceKernelGetProcessTimeWide()/10000; }
/* Android ARM mbstate_t is four bytes; newlib's is eight. Bionic's C locale
 * uses UTF-8, so never pass its caller-owned state directly to newlib. */
static __thread unsigned char multibyte_state[4];
static size_t jmbrtowc(wchar_t *out,const char *text,size_t size,unsigned char *state) {
    if(!state)state=multibyte_state;
    if(!text){out=NULL;text="";size=1;}
    unsigned char sequence[4];memcpy(sequence,state,4);
    unsigned have=0;while(have<4&&sequence[have])have++;
    size_t used=0;
    if(!have) {
        if(!size)return (size_t)-2;
        sequence[have++]=(unsigned char)text[used++];
        if(sequence[0]<0x80){if(out)*out=sequence[0];memset(state,0,4);return sequence[0]?1:0;}
    }
    unsigned lead=sequence[0],need=lead>=0xc2&&lead<=0xdf?2:lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:0;
    if(!need||have>need)goto invalid_utf;
    while(have<need) {
        if(used==size){memcpy(state,sequence,4);return (size_t)-2;}
        unsigned c=(unsigned char)text[used++];if((c&0xc0)!=0x80)goto invalid_utf;
        if(have==1 && ((lead==0xe0&&c<0xa0)||(lead==0xed&&c>0x9f)||(lead==0xf0&&c<0x90)||(lead==0xf4&&c>0x8f)))goto invalid_utf;
        sequence[have++]=c;
    }
    uint32_t value=lead&((1u<<(7-need))-1);
    for(unsigned i=1;i<need;i++){if((sequence[i]&0xc0)!=0x80)goto invalid_utf;value=(value<<6)|(sequence[i]&63);}
    if(value<(need==2?0x80u:need==3?0x800u:0x10000u)||value>0x10ffff||(value>=0xd800&&value<=0xdfff))goto invalid_utf;
    memset(state,0,4);if(out)*out=(wchar_t)value;return used;
invalid_utf:
    memset(state,0,4);acp_error(84);return (size_t)-1;
}
static size_t jwcrtomb(char *out,wchar_t wc,unsigned char *state) {
    if(state)memset(state,0,4);
    if(!out)return 1;
    uint32_t c=(uint32_t)wc;
    if(c>0x10ffff||(c>=0xd800&&c<=0xdfff)){acp_error(84);return (size_t)-1;}
    if(c<128){out[0]=c;return 1;}
    if(c<2048){out[0]=0xc0|(c>>6);out[1]=0x80|(c&63);return 2;}
    if(c<65536){out[0]=0xe0|(c>>12);out[1]=0x80|((c>>6)&63);out[2]=0x80|(c&63);return 3;}
    out[0]=0xf0|(c>>18);out[1]=0x80|((c>>12)&63);out[2]=0x80|((c>>6)&63);out[3]=0x80|(c&63);return 4;
}
static size_t jwcstombs(char *out,const wchar_t *text,size_t limit) {
    size_t bytes=0;
    for(;;text++){
        char encoded[4];size_t count=jwcrtomb(encoded,*text,NULL);
        if(count==(size_t)-1)return count;
        if(out&&count>limit-bytes)return bytes;
        if(out)memcpy(out+bytes,encoded,count);
        if(!*text)return bytes;
        bytes+=count;
    }
}
extern void __cxa_atexit(void); extern void __cxa_finalize(void); extern void __cxa_type_match(void);
_Static_assert(sizeof(z_stream)==56,"Android ARM zlib stream ABI");
_Static_assert(sizeof(jmp_buf)<=256,"Bionic ARM jump buffer must fit native state");
#define B(n,f) {n,(uintptr_t)&f}
const AcpSymbol acp_system_symbols[]={
 B("wcsftime",jwcsftime),
 B("__android_log_print",jlog_print),B("__android_log_write",jlog_write),B("__assert2",jassert),
 B("__stack_chk_guard",stack_guard),B("__stack_chk_fail",jstackfail),B("__errno",acp_errno_location),
 B("__cxa_atexit",__cxa_atexit),B("__cxa_finalize",__cxa_finalize),B("__cxa_type_match",__cxa_type_match),
 B("dl_unwind_find_exidx",acp_find_exidx),B("__gnu_Unwind_Find_exidx",acp_find_exidx),B("dlopen",acp_dlopen),B("dlsym",acp_dlsym),
 B("dlerror",acp_dlerror),B("dlclose",acp_dlclose),
 B("abort",jabort),B("exit",jexit),B("_exit",jexit),B("printf",jprintf),B("puts",jputs),B("putchar",jputchar),B("perror",jperror),
 B("strerror",jstrerror),B("strerror_r",jstrerror_r),B("time",jtime),B("clock",jclock),B("clock_gettime",acp_clock_gettime),B("gettimeofday",jgettimeofday),
 B("nanosleep",jnanosleep),B("usleep",usleep),B("gmtime",jgmtime),B("gmtime_r",jgmtime_r),B("localtime",jlocaltime),B("localtime_r",jlocaltime_r),B("mktime",jmktime),B("asctime",jasctime),B("ctime",jctime),B("strftime",jstrftime),
 B("mmap",jmmap),B("munmap",jmunmap),B("mremap",jmremap),B("mprotect",jmprotect),
 B("getenv",getenv),B("setenv",setenv),B("unsetenv",unsetenv),B("setlocale",setlocale),
 B("getpid",jgetpid),B("gethostname",jhostname),B("sysconf",jsysconf),B("sysinfo",jsysinfo),B("getrlimit",jgetrlimit),B("setrlimit",jsetrlimit),B("setpriority",jsetpriority),B("times",jtimes),
 B("execv",jenosys),B("system",jenosys),B("raise",jenosys),B("inotify_init",jenosys),B("inotify_add_watch",jenosys),B("inotify_rm_watch",jenosys),B("ioctl",jioctl),B("select",jenosys),
 B("setjmp",setjmp),B("longjmp",longjmp),B("div",div),B("lldiv",lldiv),
 B("mbrtowc",jmbrtowc),B("wcrtomb",jwcrtomb),B("wcstombs",jwcstombs),
 B("deflate",deflate),B("deflateEnd",deflateEnd),B("deflateInit_",deflateInit_),B("deflateReset",deflateReset),
 B("inflate",jinflate),B("inflateEnd",inflateEnd),B("inflateInit_",inflateInit_),B("inflateReset",inflateReset),
};
const size_t acp_system_symbol_count=sizeof(acp_system_symbols)/sizeof(acp_system_symbols[0]);
