/* Adapt the verified game's plaintext Lua chunks at their normal load boundary.
 * No decrypted game scripts or asset keys are distributed in the VPK. */
#include "acp_native.h"
#include "acp_lua.h"
#include "acp_game_patch.h"
#include "acp_lua_cache.h"
#include <stdlib.h>
#include <so_util/so_util.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <kubridge.h>
#include <string.h>
#include <math.h>
#include <psp2/io/fcntl.h>
#include "vita_scripts.inc"
#if defined(ACP_DEVELOPMENT_PROFILE) || defined(ACP_UPDATE_SAMPLES)
#include "vita_profile.inc"
extern int acp_method_register(const char *);
extern int acp_method_phase(int,int);
#endif

typedef const char *(*Reader)(lua_State *,void *,size_t *);
static int (*loadx)(lua_State *,Reader,void *,const char *,const char *);
static int (*pcall)(lua_State *,int,int,int);
static int (*dump)(lua_State *,int (*)(lua_State *,const void *,size_t,void *),void *);
static void (*pushinteger)(lua_State *,int);
static void (*pushclosure)(lua_State *,acp_lua_cfunction,int);
static void (*setfield)(lua_State *,int,const char *);
static const char *(*tolstring)(lua_State *,int,size_t *);
static void (*settop)(lua_State *,int);
static void (*getfield)(lua_State *,int,const char *);
static int (*luatype)(lua_State *,int);
static int (*tointeger)(lua_State *,int);
static double (*tonumber)(lua_State *,int);
static void (*pushlstring)(lua_State *,const char *,size_t);
static int (*gc_required)(lua_State *);
#if defined(ACP_DEVELOPMENT_PROFILE) || defined(ACP_UPDATE_SAMPLES)
static int method_register(lua_State *s){const char *name=tolstring(s,1,NULL);pushinteger(s,name?acp_method_register(name):-1);return 1;}
static int method_phase(lua_State *s){pushinteger(s,acp_method_phase(tointeger(s,1),tointeger(s,2)));return 1;}
#endif
extern void acp_frame_wait_install(void (*)(void *),void (*)(void *));
extern void acp_frame_wait(void *,unsigned,int);
extern int acp_controller_icons_load(void);
extern int acp_hold_textures_load(void);
extern volatile unsigned acp_presented_frames;
static uint32_t test_buttons,test_sticks=0x80808080,test_until;
static uint32_t consumed_buttons;

/* Test input has a short lease; a disconnected test client cannot leave a
 * button or stick held. Real Vita input is sampled on the game thread. */
void acp_vita_test_pad(unsigned buttons,unsigned sticks){
    __atomic_store_n(&test_buttons,buttons,__ATOMIC_RELAXED);
    __atomic_store_n(&test_sticks,sticks,__ATOMIC_RELAXED);
    __atomic_store_n(&test_until,sceKernelGetProcessTimeLow()+2000000,__ATOMIC_RELEASE);
}
/* Test buttons while their lease lasts (the first-launch setup screen). */
unsigned acp_vita_test_buttons(void){
    uint32_t until=__atomic_load_n(&test_until,__ATOMIC_ACQUIRE);
    return until&&(int32_t)(until-sceKernelGetProcessTimeLow())>0?__atomic_load_n(&test_buttons,__ATOMIC_RELAXED):0;
}
static int pad(lua_State *s){
    /* Several entities and Lua states read the pad in one rendered frame.
     * Give them one consistent snapshot and issue one kernel read per thread. */
    static _Thread_local unsigned sampled_frame=~0u;
    static _Thread_local SceCtrlData sampled;
    unsigned frame=acp_presented_frames;
    if(frame!=sampled_frame){
        memset(&sampled,0,sizeof(sampled));sampled.lx=sampled.ly=sampled.rx=sampled.ry=128;
        sceCtrlPeekBufferPositiveExt2(0,&sampled,1);sampled_frame=frame;
        /* Ext2 binds the Vita shoulders to L1/R1, unlike the legacy API.
         * Keep the port's logical trigger bits consistent with test input. */
        if(sampled.buttons&SCE_CTRL_L1)sampled.buttons|=SCE_CTRL_LTRIGGER;
        if(sampled.buttons&SCE_CTRL_R1)sampled.buttons|=SCE_CTRL_RTRIGGER;
    }
    SceCtrlData data=sampled;
    uint32_t until=__atomic_load_n(&test_until,__ATOMIC_ACQUIRE);
    if(until && (int32_t)(until-sceKernelGetProcessTimeLow())>0){
        data.buttons=__atomic_load_n(&test_buttons,__ATOMIC_RELAXED);
        unsigned sticks=__atomic_load_n(&test_sticks,__ATOMIC_RELAXED);
        data.lx=sticks;data.ly=sticks>>8;data.rx=sticks>>16;data.ry=sticks>>24;
    }
    unsigned consumed=__atomic_fetch_and(&consumed_buttons,data.buttons,__ATOMIC_RELAXED)&data.buttons;
    pushinteger(s,data.buttons&~consumed);pushinteger(s,data.lx);pushinteger(s,data.ly);
    pushinteger(s,data.rx);pushinteger(s,data.ry);return 5;
}
/* Leaderboards and trophies (online.c). */
extern void acp_online_submit(const char *,long long);
extern void acp_online_fetch(const char *);
extern int acp_online_result(const char *,char *,size_t);
extern const char *acp_online_user_name(void);
extern void acp_online_trophy(int);
extern int acp_online_trophy_state(void);
static int online_name(lua_State *s){const char *n=acp_online_user_name();pushlstring(s,n,strlen(n));return 1;}
static int online_submit(lua_State *s){acp_online_submit(tolstring(s,1,NULL),(long long)tonumber(s,2));return 0;}
static int online_fetch(lua_State *s){acp_online_fetch(tolstring(s,1,NULL));return 0;}
static int online_result(lua_State *s){
    static char text[4096];int status=acp_online_result(tolstring(s,1,NULL),text,sizeof(text));
    pushinteger(s,status);pushlstring(s,text,strlen(text));return 2;
}
static int trophy_unlock(lua_State *s){acp_online_trophy(tointeger(s,1));return 0;}
static int trophy_state(lua_State *s){pushinteger(s,acp_online_trophy_state());return 1;}
static int log_line(lua_State *s){const char *v=tolstring(s,1,NULL);if(v)acp_log("Vita Lua: %.400s",v);return 0;}
extern int acp_frame_phase(int,int);
#ifdef ACP_DEVELOPMENT_PROFILE
volatile unsigned acp_eval_request;
static void eval_poll(lua_State *);
#endif
static int frame_phase(lua_State *s){
#ifdef ACP_DEVELOPMENT_PROFILE
    if(tointeger(s,1)==0&&tointeger(s,2))eval_poll(s);
#endif
    pushinteger(s,acp_frame_phase(tointeger(s,1),tointeger(s,2)));return 1;
}
static int frame_wait(lua_State *s){
    double seconds=tonumber(s,1);
    if(isfinite(seconds)&&seconds>0&&seconds<=1.0){
        unsigned us=(unsigned)(seconds*1000000.0);
        if(us)acp_frame_wait(s,us,gc_required(s));
    }
    return 0;
}
#ifdef ACP_DEVELOPMENT_PROFILE
static int clock_us(lua_State *s){pushinteger(s,(int)(sceKernelGetProcessTimeWide()&0x7fffffff));return 1;}
#endif
/* ACPVitaLoading(reason, on): 1 = launch until the main menu, 2 = loading screen. */
static int loading(lua_State *s){acp_loading_boost((unsigned)tointeger(s,1),tointeger(s,2)!=0);return 0;}
static int icons(lua_State *s){pushinteger(s,tointeger(s,1)==1?acp_hold_textures_load():acp_controller_icons_load());return 1;}
static int consume(lua_State *s){__atomic_fetch_or(&consumed_buttons,(unsigned)tointeger(s,1),__ATOMIC_RELAXED);return 0;}
extern unsigned acp_preferences_get(void);
extern int acp_preferences_set(unsigned,unsigned);
static int preferences(lua_State *s){
    int option=tointeger(s,1),value=tointeger(s,2);
    if(option>=1&&option<=5&&luatype(s,2)==3){
        if(acp_preferences_set((unsigned)option-1,(unsigned)value)){pushinteger(s,-1);return 1;}
    }
    unsigned bits=acp_preferences_get();
    pushinteger(s,option>=1&&option<=5?(int)((bits>>(option-1))&1):(int)bits);return 1;
}
typedef struct {const char *prefix,*body;size_t prefix_size,body_size;} Source;
static const char *reader(lua_State *s,void *opaque,size_t *size){
    (void)s;Source *r=opaque;
    if(r->prefix_size){*size=r->prefix_size;r->prefix_size=0;return r->prefix;}
    *size=r->body_size;r->body_size=0;return *size?r->body:NULL;
}
#ifdef ACP_DEVELOPMENT_PROFILE
static void eval_poll(lua_State *s){
    static unsigned seen;
    unsigned request=__atomic_load_n(&acp_eval_request,__ATOMIC_ACQUIRE);
    if(request==seen)return;
    seen=request;
    static char code[65536];
    int fd=sceIoOpen(ACP_DATA_PATH "/api/eval.lua",SCE_O_RDONLY,0);
    if(fd<0){acp_log("Private Lua evaluation: missing file");return;}
    int n=sceIoRead(fd,code,sizeof(code));sceIoClose(fd);
    if(n<=0||n==(int)sizeof(code)){acp_log("Private Lua evaluation: invalid size");return;}
    Source r={.body=code,.body_size=(size_t)n};
    int error=loadx(s,reader,&r,"@private-api-eval","t");
    if(!error)error=pcall(s,0,0,0);
    if(error){acp_log("Private Lua evaluation failed: %s",tolstring(s,-1,NULL));settop(s,-2);}
    else acp_log("Private Lua evaluation completed: request=%u",seen);
}
#endif
static void ensure_adapter(lua_State *s,const char *name){
    /* Release assets may contain Lua bytecode. Install at the Class boundary
     * after its definition has executed; do not rewrite or prepend bytecode. */
    getfield(s,-10002,"ACPVitaWrapped");int wrapped=luatype(s,-1)==1;settop(s,-2);
    if(!wrapped){
        getfield(s,-10002,"Class");int ready=luatype(s,-1)==6;settop(s,-2);
        if(ready){
            static const char install[]="Class=ACPVitaAdapt(Class); ACPVitaWrapped=true"
#ifdef ACP_DEVELOPMENT_PROFILE
                "; if ACPVitaObjectWorkMode then assert(ACPVitaObjectWorkInstall(ACPVitaObjectWorkMode)); ACPVitaLog('Object work mode: '..ACPVitaObjectWorkMode) end"
#endif
                ;
            Source init={0};init.body=install;init.body_size=sizeof(install)-1;
            int result=loadx(s,reader,&init,"@vita-class-adapter","t");
            if(!result)result=pcall(s,0,0,0);
            if(result)acp_fatal("Vita Class adapter: %s",tolstring(s,-1,NULL));
            acp_log("Vita Class adapter installed before %s",name?name:"unnamed chunk");
        }
    }
    /* Class is defined before CreateInstance. A nested module load can reach
     * this hook between those definitions, so retry the idempotent factory
     * installer on subsequent module loads rather than losing it forever. */
    getfield(s,-10002,"ACPVitaEntityStateInstall");
    if(luatype(s,-1)==6){
        if(pcall(s,0,0,0))acp_fatal("Entity factory adapter: %s",tolstring(s,-1,NULL));
    }else settop(s,-2);
}
/* Load-time accounting: chunks, bytes and time inside the original compiler.
 * Game thread only; reported with the regular frame statistics. */
static struct {unsigned chunks,source,bytecode,readers,max_us,unique,repeats,cached;uint64_t bytes,us,reader_bytes,reader_us;} loads;
static unsigned reported_chunks;
static uint32_t seen_names[8192];
void acp_lua_load_report(void){
    if(loads.chunks==reported_chunks)return;
    reported_chunks=loads.chunks;
    acp_log("Lua chunks: loaded=%u unique=%u repeated=%u source=%u bytecode=%u reader=%u bytes=%llu compile=%llu us max=%u us reader-bytes=%llu reader-time=%llu us",
        loads.chunks,loads.unique,loads.repeats,loads.source,loads.bytecode,loads.readers,loads.bytes,loads.us,loads.max_us,
        loads.reader_bytes,loads.reader_us);
    acp_lua_cache_stats c=acp_lua_cache_stats_get();
    acp_log("Lua bytecode cache: loaded-from-cache=%u hits=%u misses=%u stored=%u rejected=%u read=%u KiB file=%u KiB",
        loads.cached,c.hits,c.misses,c.stored,c.rejected,c.loaded_bytes/1024,c.file_bytes/1024);
}
static void load_account(uint64_t begin,const char *name){
    unsigned elapsed=(unsigned)(sceKernelGetProcessTimeWide()-begin);
    loads.chunks++;loads.us+=elapsed;if(elapsed>loads.max_us)loads.max_us=elapsed;
    if(!name)return;
    uint32_t h=2166136261u;for(const char *c=name;*c;c++)h=(h^(uint8_t)*c)*16777619u;
    if(!h)h=1;
    for(unsigned i=0;i<8192;i++){
        uint32_t *slot=&seen_names[(h+i)&8191];
        if(*slot==h){loads.repeats++;return;}
        if(!*slot){*slot=h;loads.unique++;return;}
    }
}
/* Persistent bytecode for the game's own chunks (see acp_lua_cache.h).
 * Parsing ~13 MB of source took ~7 s per launch on the Vita. The cache keeps
 * only its index and a 256 KiB read window, from the fixed newlib heap: a
 * separate memory block before graphics start-up consumed the free-memory
 * headroom vitaGL requires (00.61 failed to start with a 6 MB cache). */
static int cache_opened;
static void *cache_allocate(size_t n){return malloc(n);}
static void cache_release(void *p){free(p);}
typedef struct {unsigned char *p;size_t n,cap;int failed;} Dump;
static int dump_write(lua_State *s,const void *p,size_t n,void *ud){
    (void)s;Dump *d=ud;
    if(d->failed)return 1;
    if(d->n+n>d->cap){
        size_t cap=d->cap?d->cap*2:16384;while(cap<d->n+n)cap*=2;
        unsigned char *q=realloc(d->p,cap);if(!q){d->failed=1;return 1;}
        d->p=q;d->cap=cap;
    }
    memcpy(d->p+d->n,p,n);d->n+=n;return 0;
}
static int load_source(lua_State *s,const char *p,size_t n,const char *name,const char *mode){
    Source r={0};r.body=p;r.body_size=n;
    if(n<256||p[0]==0x1b||n>0xffffffffu)return loadx(s,reader,&r,name,mode);
    if(!cache_opened){
        static const acp_lua_cache_memory memory={cache_allocate,cache_release};
        acp_lua_cache_open(ACP_DATA_PATH "/lua-bytecode-v1.bin","LJ202ACP",16u<<20,&memory);
        cache_opened=1;
    }
    uint64_t key=acp_lua_cache_key(name,p,n);uint32_t size=0;
    const void *code=acp_lua_cache_find(key,(uint32_t)n,&size);
    if(code){
        Source b={0};b.body=code;b.body_size=size;
        if(!loadx(s,reader,&b,name,"b")){loads.cached++;return 0;}
        settop(s,-2);acp_lua_cache_reject(key,(uint32_t)n);
    }
    int result=loadx(s,reader,&r,name,mode);
    if(!result&&dump){
        Dump d={0};
        if(!dump(s,dump_write,&d)&&!d.failed&&d.n<=0xffffffffu)acp_lua_cache_store(key,(uint32_t)n,d.p,(uint32_t)d.n);
        free(d.p);
    }
    return result;
}
static int load_buffer(lua_State *s,const char *p,size_t n,const char *name,const char *mode){
    ensure_adapter(s,name);
    if(n&&p[0]==0x1b)loads.bytecode++;else loads.source++;
    loads.bytes+=n;
    uint64_t begin=sceKernelGetProcessTimeWide();
    int result=load_source(s,p,n,name,mode);
    load_account(begin,name);
    return result;
}
/* Separates the game's own reader (archive/decode work) from parsing. */
typedef struct {Reader read;void *opaque;uint64_t us,bytes;} TimedReader;
static const char *timed_read(lua_State *s,void *data,size_t *size){
    TimedReader *t=data;uint64_t begin=sceKernelGetProcessTimeWide();
    const char *p=t->read(s,t->opaque,size);
    t->us+=sceKernelGetProcessTimeWide()-begin;if(p)t->bytes+=*size;
    return p;
}
/* Allocation-failure fallback: hand the collected prefix to the parser, then
 * continue with the game's reader, exactly as an uncached load would. */
typedef struct {const char *part[2];size_t size[2];TimedReader *rest;} ChainReader;
static const char *chain_read(lua_State *s,void *data,size_t *size){
    ChainReader *c=data;
    for(unsigned i=0;i<2;i++)if(c->size[i]){*size=c->size[i];c->size[i]=0;return c->part[i];}
    return timed_read(s,c->rest,size);
}
static int load_reader(lua_State *s,Reader read,void *opaque,const char *name){
    ensure_adapter(s,name);
    loads.readers++;
    TimedReader timed={read,opaque,0,0};
    uint64_t begin=sceKernelGetProcessTimeWide();
    /* Collect the complete chunk first so it can be keyed; the game's reader
     * only hands over already decoded text. */
    char *text=NULL;size_t n=0,cap=0;
    const char *pending=NULL;size_t pending_size=0;
    for(;;){
        size_t k=0;const char *p=timed_read(s,&timed,&k);
        if(!p||!k)break;
        if(n+k>cap){
            size_t next=cap?cap*2:65536;while(next<n+k)next*=2;
            char *q=realloc(text,next);
            if(!q){pending=p;pending_size=k;break;}
            text=q;cap=next;
        }
        memcpy(text+n,p,k);n+=k;
    }
    int result;
    if(pending){
        ChainReader chain={{text,pending},{n,pending_size},&timed};
        result=loadx(s,chain_read,&chain,name,NULL);
    }else if(n)result=load_source(s,text,n,name,NULL);
    else{Source r={0};result=loadx(s,reader,&r,name,NULL);}
    free(text);
    load_account(begin,name);
    loads.reader_us+=timed.us;loads.reader_bytes+=timed.bytes;
    return result;
}
int acp_vita_scripts_prepare(lua_State *s){
    pushclosure(s,frame_phase,0);setfield(s,-10002,"ACPVitaFramePhase");
    pushclosure(s,frame_wait,0);setfield(s,-10002,"ACPVitaFrameWait");
    pushclosure(s,pad,0);setfield(s,-10002,"ACPVitaPad");
    pushclosure(s,log_line,0);setfield(s,-10002,"ACPVitaLog");
    pushclosure(s,icons,0);setfield(s,-10002,"ACPVitaIcons");
    pushclosure(s,consume,0);setfield(s,-10002,"ACPVitaConsume");
    pushclosure(s,preferences,0);setfield(s,-10002,"ACPVitaSettings");
    pushclosure(s,loading,0);setfield(s,-10002,"ACPVitaLoading");
    pushclosure(s,online_name,0);setfield(s,-10002,"ACPVitaUserName");
    pushclosure(s,online_submit,0);setfield(s,-10002,"ACPVitaLeaderboardSubmit");
    pushclosure(s,online_fetch,0);setfield(s,-10002,"ACPVitaLeaderboardFetch");
    pushclosure(s,online_result,0);setfield(s,-10002,"ACPVitaLeaderboardResult");
    pushclosure(s,trophy_unlock,0);setfield(s,-10002,"ACPVitaTrophyUnlock");
    pushclosure(s,trophy_state,0);setfield(s,-10002,"ACPVitaTrophyState");
#ifdef ACP_DEVELOPMENT_PROFILE
    /* Emulator loading profiles: vita_sampler.lua runs only with this clock. */
    int sampler=sceIoOpen(ACP_DATA_PATH "/lua-sampler.flag",SCE_O_RDONLY,0);
    if(sampler>=0){sceIoClose(sampler);pushclosure(s,clock_us,0);setfield(s,-10002,"ACPVitaClock");}
#endif
    Source r={0};r.body=acp_vita_lua;r.body_size=sizeof(acp_vita_lua)-1;
    int result=loadx(s,reader,&r,"@vita-controls","t");
    if(!result)result=pcall(s,0,0,0);
    if(result){acp_log("Vita Lua initialization failed: %s",tolstring(s,-1,NULL));settop(s,-2);}
#ifdef ACP_DEVELOPMENT_PROFILE
    int object_mode=sceIoOpen(ACP_DATA_PATH "/object-work.flag",SCE_O_RDONLY,0);
    if(object_mode>=0){
        char mode=0;sceIoRead(object_mode,&mode,1);sceIoClose(object_mode);
        int api=sceIoOpen(ACP_DATA_PATH "/emulator-api.flag",SCE_O_RDONLY,0);
        if(api>=0){sceIoClose(api);if(mode>='1'&&mode<='3'){pushinteger(s,mode-'0');setfield(s,-10002,"ACPVitaObjectWorkMode");}}
    }
    int flag=sceIoOpen(ACP_DATA_PATH "/method-profile.flag",SCE_O_RDONLY,0);
    if(flag>=0){
        sceIoClose(flag);flag=sceIoOpen(ACP_DATA_PATH "/emulator-api.flag",SCE_O_RDONLY,0);
        if(flag>=0){
            sceIoClose(flag);
            pushinteger(s,1);setfield(s,-10002,"ACPVitaTraceEvents");
            pushclosure(s,method_register,0);setfield(s,-10002,"ACPVitaMethodRegister");
            pushclosure(s,method_phase,0);setfield(s,-10002,"ACPVitaMethodPhase");
            int policy=sceIoOpen(ACP_DATA_PATH "/jit-retry.flag",SCE_O_RDONLY,0);
            if(policy>=0){sceIoClose(policy);pushinteger(s,1);setfield(s,-10002,"ACPVitaJitRetryEnabled");}
            r=(Source){.body=acp_profile_lua,.body_size=sizeof(acp_profile_lua)-1};
            result=loadx(s,reader,&r,"@vita-profile","t");
            if(!result)result=pcall(s,0,0,0);
            if(result)acp_fatal("Method profiler: %s",tolstring(s,-1,NULL));
        }
    }
#endif
#ifdef ACP_UPDATE_SAMPLES
    /* No trace observer, imports, flags, VM replacement, or per-method hooks. */
    pushclosure(s,method_register,0);setfield(s,-10002,"ACPVitaMethodRegister");
    pushclosure(s,method_phase,0);setfield(s,-10002,"ACPVitaMethodPhase");
    pushinteger(s,1);setfield(s,-10002,"ACPVitaBoundedSamples");
    r=(Source){.body=acp_profile_lua,.body_size=sizeof(acp_profile_lua)-1};
    result=loadx(s,reader,&r,"@vita-update-samples","t");
    if(!result)result=pcall(s,0,0,0);
    if(result)acp_fatal("Update sampler: %s",tolstring(s,-1,NULL));
#endif
    return result;
}
int acp_vita_scripts_install(so_module *m){
    loadx=(void *)so_symbol(m,"lua_loadx");pcall=(void *)so_symbol(m,"lua_pcall");dump=(void *)so_symbol(m,"lua_dump");
    pushinteger=(void *)so_symbol(m,"lua_pushinteger");pushclosure=(void *)so_symbol(m,"lua_pushcclosure");
    setfield=(void *)so_symbol(m,"lua_setfield");tolstring=(void *)so_symbol(m,"lua_tolstring");settop=(void *)so_symbol(m,"lua_settop");
    getfield=(void *)so_symbol(m,"lua_getfield");luatype=(void *)so_symbol(m,"lua_type");
    tointeger=(void *)so_symbol(m,"lua_tointeger");
    tonumber=(void *)so_symbol(m,"lua_tonumber");
    pushlstring=(void *)so_symbol(m,"lua_pushlstring");
    extern int acp_gc_window_required(void *);
    gc_required=(void *)acp_gc_window_required;
    uintptr_t start_gc=so_symbol(m,"_ZN13LuaBindTools227StartAsyncGarbageCollectionEP9lua_State");
    uintptr_t stop_gc=so_symbol(m,"_ZN13LuaBindTools226StopAsyncGarbageCollectionEP9lua_State");
    if(!tonumber||!gc_required||!start_gc||!stop_gc)return -1;
    acp_frame_wait_install((void *)start_gc,(void *)stop_gc);
    uintptr_t address=so_symbol(m,"luaL_loadbufferx"),read_address=so_symbol(m,"lua_load");
    if(!loadx||!pcall||!pushinteger||!pushclosure||!setfield||!tolstring||!settop||!getfield||!luatype||!tointeger||!pushlstring||!address||!read_address)return -1;
    uint8_t jump[8];if(acp_game_patch_build((uint32_t)(uintptr_t)load_buffer,jump))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)(address&~1u),jump,sizeof(jump))<0)return -1;
    if(acp_game_patch_build((uint32_t)(uintptr_t)load_reader,jump))return -1;
    return kuKernelCpuUnrestrictedMemcpy((void *)(read_address&~1u),jump,sizeof(jump))<0?-1:0;
}
