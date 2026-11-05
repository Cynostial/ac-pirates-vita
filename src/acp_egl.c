#include "acp_egl.h"
#include <string.h>
#include <stddef.h>

enum { FALSE_=0,TRUE_=1,SUCCESS=0x3000,NOT_INITIALIZED=0x3001,
    BAD_ACCESS=0x3002,BAD_ALLOC=0x3003,BAD_ATTRIBUTE=0x3004,
    BAD_CONFIG=0x3005,BAD_CONTEXT=0x3006,BAD_DISPLAY=0x3008,
    BAD_MATCH=0x3009,BAD_WINDOW=0x300b,BAD_PARAMETER=0x300c,
    BAD_SURFACE=0x300d,NONE=0x3038 };
static acp_egl_backend api;
static struct {
    uintptr_t owner;
    int initialized,attempted,context,surface,current;
    int context_deleted,surface_deleted;
    void *window;
} g;
static _Thread_local int error=SUCCESS;
static char display_id,config_id,context_id,surface_id;
#define DISPLAY ((void *)&display_id)
#define CONFIG ((void *)&config_id)
#define CONTEXT ((void *)&context_id)
#define SURFACE ((void *)&surface_id)
static unsigned fail(int e){error=e;return FALSE_;}
static void *fail_ptr(int e){fail(e);return NULL;}
static int valid(void *d){
    if(d!=DISPLAY)return (int)fail(BAD_DISPLAY);
    if(!g.initialized)return (int)fail(NOT_INITIALIZED);
    if(api.thread_id()!=g.owner)return (int)fail(BAD_ACCESS);
    return 1;
}
int acp_egl_install(const acp_egl_backend *b){
    if(api.thread_id||!b||!b->thread_id||!b->initialize||!b->swap||
       !b->interval||!b->window_valid||!b->resolve_gl)return -1;
    api=*b;return 0;
}
int acp_egl_current(void){return api.thread_id&&g.current&&api.thread_id()==g.owner;}
static void *acp_eglGetDisplay(void *native){return native?fail_ptr(BAD_PARAMETER):DISPLAY;}
static unsigned acp_eglInitialize(void *d,int *major,int *minor){
    if(d!=DISPLAY)return fail(BAD_DISPLAY);
    if(!api.thread_id)return fail(NOT_INITIALIZED);
    if(g.initialized&&g.owner!=api.thread_id())return fail(BAD_ACCESS);
    if(!g.initialized){
        if(g.attempted)return fail(BAD_ALLOC);
        g.attempted=1;
        if(api.initialize())return fail(BAD_ALLOC);
        g.initialized=1;g.owner=api.thread_id();
    }
    if(major)*major=1;
    if(minor)*minor=4;
    return TRUE_;
}
static int acp_eglGetError(void){int e=error;error=SUCCESS;return e;}
static int config_value(int attr,int *out){
    switch(attr){
    case 0x3020:*out=32;break; /* buffer */
    case 0x3021:case 0x3022:case 0x3023:case 0x3024:case 0x3026:*out=8;break;
    case 0x3025:*out=24;break; /* guaranteed depth precision */
    case 0x3027:*out=NONE;break; /* caveat */
    case 0x3028:case 0x302e:*out=1;break; /* config / native RGBA8888 */
    case 0x3033:case 0x3040:case 0x3042:*out=4;break; /* window / ES2 */
    case 0x3039:*out=1;break; /* min swap interval */
    case 0x303a:*out=2;break; /* max interval: 30 fps */
    case 0x3029:case 0x302d:case 0x302f:case 0x3031:case 0x3032:*out=0;break;
    case 0x3034:*out=NONE;break; /* transparent type */
    case 0x303f:*out=0x308e;break; /* RGB buffer */
    default:return 0;
    }
    return 1;
}
static unsigned acp_eglGetConfigAttrib(void *d,void *c,int a,int *v){
    if(!valid(d))return FALSE_;
    if(c!=CONFIG)return fail(BAD_CONFIG);
    if(!v)return fail(BAD_PARAMETER);
    return config_value(a,v)?TRUE_:fail(BAD_ATTRIBUTE);
}
static unsigned acp_eglChooseConfig(void *d,const int *attrs,void **out,int capacity,int *n){
    if(!valid(d))return FALSE_;
    if(!n||capacity<0)return fail(BAD_PARAMETER);
    int match=1;
    if(attrs)for(unsigned i=0;;i+=2){
        if(i>=128)return fail(BAD_ATTRIBUTE);
        int a=attrs[i],have,want;
        if(a==NONE)break;
        want=attrs[i+1];
        if(!config_value(a,&have))return fail(BAD_ATTRIBUTE);
        if(want==-1)continue; /* DONT_CARE */
        if(a==0x3033||a==0x3040||a==0x3042)match&=(have&want)==want;
        else if((a>=0x3020&&a<=0x3026)||a==0x3031||a==0x3032)match&=have>=want;
        else match&=have==want;
    }
    *n=match&&(!out||capacity>0);
    if(out&&capacity>0&&match)*out=CONFIG;
    return TRUE_;
}
static void *acp_eglCreateContext(void *d,void *c,void *share,const int *attrs){
    if(!valid(d))return NULL;
    if(c!=CONFIG)return fail_ptr(BAD_CONFIG);
    if(share||g.context)return fail_ptr(BAD_MATCH);
    int version=1;
    if(attrs)for(unsigned i=0;;i+=2){
        if(i>=32)return fail_ptr(BAD_ATTRIBUTE);
        if(attrs[i]==NONE)break;
        if(attrs[i]!=0x3098)return fail_ptr(BAD_ATTRIBUTE);
        version=attrs[i+1];
    }
    if(version!=2)return fail_ptr(BAD_MATCH);
    g.context=1;g.context_deleted=0;return CONTEXT;
}
static void *acp_eglCreateWindowSurface(void *d,void *c,void *w,const int *attrs){
    if(!valid(d))return NULL;
    if(c!=CONFIG)return fail_ptr(BAD_CONFIG);
    if(!w||!api.window_valid(w))return fail_ptr(BAD_WINDOW);
    if(g.surface)return fail_ptr(BAD_ALLOC);
    if(attrs&&attrs[0]!=NONE)return fail_ptr(BAD_ATTRIBUTE);
    g.window=w;g.surface=1;g.surface_deleted=0;return SURFACE;
}
static void release_deleted(void){
    if(g.context_deleted){g.context=0;g.context_deleted=0;}
    if(g.surface_deleted){g.surface=0;g.surface_deleted=0;g.window=NULL;}
}
static unsigned acp_eglMakeCurrent(void *d,void *draw,void *read,void *c){
    if(!valid(d))return FALSE_;
    if(!c&&!draw&&!read){g.current=0;release_deleted();return TRUE_;}
    if(!g.context||c!=CONTEXT||g.context_deleted)return fail(BAD_CONTEXT);
    if(!g.surface||draw!=SURFACE||read!=SURFACE||g.surface_deleted)return fail(BAD_SURFACE);
    g.current=1;return TRUE_;
}
static unsigned acp_eglSwapBuffers(void *d,void *s){
    if(!valid(d))return FALSE_;
    if(s!=SURFACE||!g.surface)return fail(BAD_SURFACE);
    if(!g.current)return fail(BAD_CONTEXT);
    api.swap();return TRUE_;
}
static unsigned acp_eglSwapInterval(void *d,int n){
    if(!valid(d))return FALSE_;
    if(!g.current)return fail(BAD_CONTEXT);
    /* EGL clamps to the advertised min/max. Keep display pacing enabled. */
    api.interval(n<1?1:n>2?2:n);return TRUE_;
}
static unsigned acp_eglDestroyContext(void *d,void *c){
    if(!valid(d))return FALSE_;
    if(c!=CONTEXT||!g.context||g.context_deleted)return fail(BAD_CONTEXT);
    g.context_deleted=1;
    if(!g.current)release_deleted();
    return TRUE_;
}
static unsigned acp_eglDestroySurface(void *d,void *s){
    if(!valid(d))return FALSE_;
    if(s!=SURFACE||!g.surface||g.surface_deleted)return fail(BAD_SURFACE);
    g.surface_deleted=1;
    if(!g.current)release_deleted();
    return TRUE_;
}
static unsigned acp_eglTerminate(void *d){
    if(!valid(d))return FALSE_;
    /* The engine may recreate its EGL display after an Android window event.
     * Keep physical VitaGL pools alive; no second backing allocation. */
    if(g.current)return fail(BAD_ACCESS);
    g.context=g.surface=g.context_deleted=g.surface_deleted=0;g.window=NULL;
    return TRUE_;
}
static void *acp_eglGetCurrentContext(void){return acp_egl_current()?CONTEXT:NULL;}
static void *acp_eglGetCurrentDisplay(void){return acp_egl_current()?DISPLAY:NULL;}
static void *acp_eglGetProcAddress(const char *name){return (void *)acp_egl_resolve(name);}
static const char *acp_eglQueryString(void *d,int name){
    if(!valid(d))return NULL;
    switch(name){
    case 0x3053:return "ACP VitaGL";
    case 0x3054:return "1.4";
    case 0x3055:return "";
    case 0x308d:return "OpenGL_ES";
    default:fail(BAD_PARAMETER);return NULL;
    }
}
uintptr_t acp_egl_resolve(const char *name){
    if(!name)return 0;
#define BIND(n) if(!strcmp(name,#n))return (uintptr_t)&acp_##n
    BIND(eglGetDisplay);BIND(eglInitialize);BIND(eglGetError);BIND(eglChooseConfig);
    BIND(eglGetConfigAttrib);BIND(eglCreateContext);BIND(eglCreateWindowSurface);
    BIND(eglMakeCurrent);BIND(eglSwapBuffers);BIND(eglSwapInterval);
    BIND(eglDestroyContext);BIND(eglDestroySurface);BIND(eglTerminate);
    BIND(eglGetProcAddress);BIND(eglQueryString);BIND(eglGetCurrentContext);BIND(eglGetCurrentDisplay);
#undef BIND
    return api.resolve_gl?api.resolve_gl(name):0;
}
