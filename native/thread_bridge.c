/* Bionic ARM32 pthread ABI bridge. Original code, MIT. */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "acp_native.h"

typedef uint32_t BionicHandle;
typedef struct { pthread_attr_t native; uint32_t reserved[5]; } BionicAttr;
typedef struct { pthread_cond_t native; int monotonic; } Cond;
_Static_assert(sizeof(pthread_mutex_t) == 4 && sizeof(pthread_key_t) == 4, "ARM32 pthread ABI");
_Static_assert(sizeof(BionicAttr) == 24, "Bionic pthread_attr_t ABI");

static int result(int r) { return acp_errno_code(r); }
static int mutex_make(BionicHandle *p, const uint32_t *attr) {
    if (!p) return 22;
    uint32_t old = __atomic_load_n(p, __ATOMIC_ACQUIRE);
    if (old > 0xffff) return 0;
    if (old & 0x2000) return 38; /* process-shared objects unsupported on Vita */
    int type = attr ? (int)(*attr & 15) : (int)((old >> 14) & 3);
    if (type > 2) return 22;
    pthread_mutexattr_t a;
    int r = pthread_mutexattr_init(&a);
    if (r) return result(r);
    r = pthread_mutexattr_settype(&a, type);
    pthread_mutex_t value = NULL;
    if (!r) r = pthread_mutex_init(&value, &a);
    pthread_mutexattr_destroy(&a);
    if (r) return result(r);
    uint32_t expected = old;
    if (!__atomic_compare_exchange_n(p, &expected, (uint32_t)value, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
        pthread_mutex_destroy(&value);
    return 0;
}
static int jm_init(BionicHandle *p, const uint32_t *a) {
    if (!p) return 22;
    *p = 0;
    return mutex_make(p, a);
}
static int jm_lock(BionicHandle *p) {
    int r=mutex_make(p,NULL);if(!r)r=result(pthread_mutex_lock((pthread_mutex_t*)p));
    return r;
}
static int jm_trylock(BionicHandle *p) { int r=mutex_make(p,NULL); return r?r:result(pthread_mutex_trylock((pthread_mutex_t*)p)); }
static int jm_unlock(BionicHandle *p) { if (!p || *p<=0xffff) return 22; return result(pthread_mutex_unlock((pthread_mutex_t*)p)); }
static int jm_destroy(BionicHandle *p) {
    if (!p) return 22;
    if (*p<=0xffff) { *p=0; return 0; }
    int r=pthread_mutex_destroy((pthread_mutex_t*)p);
    if (!r) *p=0;
    return result(r);
}
static int jma_init(uint32_t *p) { if (!p) return 22; *p=0; return 0; }
static int jma_type(uint32_t *p,int type) { if (!p || type<0 || type>2) return 22; *p=(*p&~15u)|(unsigned)type; return 0; }
static int jma_destroy(uint32_t *p) { if (!p) return 22; *p=0; return 0; }

static int cond_make(BionicHandle *p, const uint32_t *attr) {
    if (!p) return 22;
    uint32_t old=__atomic_load_n(p,__ATOMIC_ACQUIRE);
    if (old>0xffff) return 0;
    if (old & 1) return 38;
    Cond *c=calloc(1,sizeof(*c));
    if (!c) return 12;
    c->monotonic = attr ? !!(*attr & 2) : !!(old & 2);
    int r=pthread_cond_init(&c->native,NULL);
    if (r) { free(c); return result(r); }
    uint32_t expected=old;
    if (!__atomic_compare_exchange_n(p,&expected,(uint32_t)c,0,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE)) {
        pthread_cond_destroy(&c->native); free(c);
    }
    return 0;
}
static int jc_init(BionicHandle *p,const uint32_t *a) { if (!p) return 22; *p=0; return cond_make(p,a); }
static int jc_signal(BionicHandle *p) { int r=cond_make(p,NULL); return r?r:result(pthread_cond_signal(&((Cond*)*p)->native)); }
static int jc_broadcast(BionicHandle *p) { int r=cond_make(p,NULL); return r?r:result(pthread_cond_broadcast(&((Cond*)*p)->native)); }
static int jc_destroy(BionicHandle *p) {
    if (!p) return 22;
    if (*p<=0xffff) { *p=0; return 0; }
    Cond *c=(Cond*)*p; int r=pthread_cond_destroy(&c->native);
    if (!r) { free(c); *p=0; }
    return result(r);
}
static int jc_wait(BionicHandle *p,BionicHandle *m) {
    int r=cond_make(p,NULL); if (!r) r=mutex_make(m,NULL); if (r) return r;
    r=result(pthread_cond_wait(&((Cond*)*p)->native,(pthread_mutex_t*)m));
    return r;
}
static int cond_timed(BionicHandle *p,BionicHandle *m,const AcpTimespec *t,uintptr_t caller) {
    if (!t || t->nsec<0 || t->nsec>=1000000000) return 22;
    int r=cond_make(p,NULL); if (!r) r=mutex_make(m,NULL); if (r) return r;
    Cond *c=(Cond*)*p;
    struct timespec native={t->sec,t->nsec};
    if (c->monotonic) {
        AcpTimespec mono,real;
        acp_clock_gettime(1,&mono); acp_clock_gettime(0,&real);
        int64_t ns=((int64_t)t->sec-mono.sec+real.sec)*1000000000+t->nsec-mono.nsec+real.nsec;
        if (ns<0) return 110;
        native.tv_sec=ns/1000000000; native.tv_nsec=ns%1000000000;
    }
    (void)caller;
    r=result(pthread_cond_timedwait(&c->native,(pthread_mutex_t*)m,&native));
    
    return r;
}
static int jc_timed(BionicHandle *p,BionicHandle *m,const AcpTimespec *t) {
    return cond_timed(p,m,t,(uintptr_t)__builtin_return_address(0));
}
static int jc_relative(BionicHandle *p,BionicHandle *m,const AcpTimespec *t) {
    if (!t || t->sec<0 || t->nsec<0 || t->nsec>=1000000000) return 22;
    int r=cond_make(p,NULL); if (r) return r;
    AcpTimespec now;
    acp_clock_gettime(((Cond*)*p)->monotonic?1:0,&now);
    int64_t ns=(int64_t)now.nsec+t->nsec;
    AcpTimespec deadline={now.sec+t->sec+(int32_t)(ns/1000000000),(int32_t)(ns%1000000000)};
    return cond_timed(p,m,&deadline,(uintptr_t)__builtin_return_address(0));
}
static int ja_init(BionicAttr *a) {
    if (!a) return 22;
    memset(a,0,sizeof(*a));
    int r=pthread_attr_init(&a->native);
    if (!r) r=pthread_attr_setstacksize(&a->native,1024*1024);
    return result(r);
}
static int ja_destroy(BionicAttr *a) { if (!a) return 22; return result(pthread_attr_destroy(&a->native)); }
static int ja_stack(BionicAttr *a,size_t bytes) { if (!a) return 22; return result(pthread_attr_setstacksize(&a->native,bytes)); }
static int ja_detach(BionicAttr *a,int state) {
    if (!a || (state!=0 && state!=1)) return 22;
    return result(pthread_attr_setdetachstate(&a->native,state?PTHREAD_CREATE_DETACHED:PTHREAD_CREATE_JOINABLE));
}
static int jt_create(pthread_t *t,const BionicAttr *a,void *(*fn)(void*),void *arg) {
    if (a) return result(pthread_create(t,&a->native,fn,arg));
    BionicAttr temporary; int r=ja_init(&temporary); if (r) return r;
    r=result(pthread_create(t,&temporary.native,fn,arg)); ja_destroy(&temporary); return r;
}
static int jt_once(uint32_t *once,void (*fn)(void)) {
    if (!once || !fn) return 22;
    /* The overwhelmingly common path needs only acquire visibility of the
     * completed initializer, not a failed CAS and a timestamped wait sample. */
    if(__atomic_load_n(once,__ATOMIC_ACQUIRE)==2)return 0;
    uint32_t expected=0;
    if (__atomic_compare_exchange_n(once,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        fn(); __atomic_store_n(once,2,__ATOMIC_RELEASE);
    } else if(expected!=2) {
        while (__atomic_load_n(once,__ATOMIC_ACQUIRE)!=2) sceKernelDelayThread(100);
        
    }
    return 0;
}
static int jt_name(pthread_t t,const char *name) {
    if (!t || !name) return 22;
    if (strlen(name)>15) return 34;
    acp_log("Thread %08x name: %s",(unsigned)t,name);
    return 0;
}
#define B(n,f) {n,(uintptr_t)&f}
const AcpSymbol acp_thread_symbols[]={
    B("pthread_create",jt_create),B("pthread_once",jt_once),B("pthread_setname_np",jt_name),
    B("pthread_attr_init",ja_init),B("pthread_attr_destroy",ja_destroy),
    B("pthread_attr_setstacksize",ja_stack),B("pthread_attr_setdetachstate",ja_detach),
    B("pthread_mutex_init",jm_init),B("pthread_mutex_lock",jm_lock),B("pthread_mutex_trylock",jm_trylock),
    B("pthread_mutex_unlock",jm_unlock),B("pthread_mutex_destroy",jm_destroy),
    B("pthread_mutexattr_init",jma_init),B("pthread_mutexattr_settype",jma_type),B("pthread_mutexattr_destroy",jma_destroy),
    B("pthread_cond_init",jc_init),B("pthread_cond_destroy",jc_destroy),B("pthread_cond_signal",jc_signal),
    B("pthread_cond_broadcast",jc_broadcast),B("pthread_cond_wait",jc_wait),B("pthread_cond_timedwait",jc_timed),
    B("pthread_cond_timedwait_relative_np",jc_relative),
    B("pthread_detach",pthread_detach),B("pthread_equal",pthread_equal),B("pthread_exit",pthread_exit),
    B("pthread_getschedparam",pthread_getschedparam),B("pthread_setschedparam",pthread_setschedparam),
    B("pthread_getspecific",pthread_getspecific),B("pthread_join",pthread_join),B("pthread_key_create",pthread_key_create),
    B("pthread_key_delete",pthread_key_delete),B("pthread_self",pthread_self),B("pthread_setspecific",pthread_setspecific),
    B("sched_get_priority_max",sched_get_priority_max),B("sched_get_priority_min",sched_get_priority_min),B("sched_yield",sched_yield),
};
const size_t acp_thread_symbol_count=sizeof(acp_thread_symbols)/sizeof(acp_thread_symbols[0]);
