#include "acp_graphics.h"
#include "acp_egl.h"
#include "acp_popup.h"
#include "acp_glsl_compat.h"
#include "acp_native.h"
#include "acp_fps.h"
#include "acp_game_patch.h"
#include <vitaGL.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void *window;
static acp_shader_log logger;
static void (*status)(const char *);
volatile unsigned acp_presented_frames;
volatile unsigned acp_draw_calls;
extern void acp_capture_frame(unsigned);
extern int acp_shader_trace_enabled(void);
extern void acp_shader_trace(const char *,unsigned,unsigned,int);
extern void acp_vitagl_water_trace(void);
extern void acp_vitagl_water_compare(int);
extern void acp_vitagl_water_forget(unsigned);
extern char *acp_water_shader(const char *);
__attribute__((noinline)) void acp_render_checkpoint(void){__asm__ volatile("":::"memory");}
void acp_graphics_set_status(void (*callback)(const char *)){status=callback;}
static GLenum pending_error;
static const char extensions[]=
    "GL_OES_compressed_ETC1_RGB8_texture GL_OES_depth24 "
    "GL_OES_packed_depth_stencil GL_OES_rgb8_rgba8 GL_OES_texture_npot";
static uintptr_t thread_id(void){return (uintptr_t)sceKernelGetThreadId();}
static unsigned display_rate;
static unsigned draw_sample_count,draw_sample_us,draw_sample_max;
typedef struct { unsigned count,total,maximum; } RenderWait;
static RenderWait render_waits[3]; /* clear, flush, readback; one frame in 16 */
static void render_wait_end(unsigned index,uint64_t start){
    unsigned elapsed=(unsigned)(sceKernelGetProcessTimeWide()-start);
    RenderWait *s=render_waits+index;
    s->count++;s->total+=elapsed;if(elapsed>s->maximum)s->maximum=elapsed;
}
extern unsigned acp_preferences_get(void);
extern void acp_frame_profile_report(void);
extern void acp_native_heap_report(void);
extern void acp_texture_flip_report(void);
extern void acp_contact_reports_log(void);
extern void acp_animation_stats_log(void);
extern void acp_engine_timing_report(void);
extern void acp_matrix_report(void);
extern void acp_thread_timing_report(void);
extern void acp_lua_load_report(void);
static void display_fps(void *framebuffer){
    static acp_fps counter;
    acp_fps_draw(&counter,(acp_preferences_get()&2)?framebuffer:NULL,960,544,960,sceKernelGetProcessTimeWide());
    if(counter.ready)__atomic_store_n(&display_rate,counter.tenths,__ATOMIC_RELAXED);
}
static int initialize(void){
    SceKernelFreeMemorySizeInfo info={.size=sizeof(info)};
    if(sceKernelGetFreeMemorySize(&info)<0){
        if(status)status("VitaGL initialization failed: free-memory query");
        return -1;
    }
    if(status){
        char text[160];
        snprintf(text,sizeof(text),"VitaGL initialization: free main %u KiB, CDRAM %u KiB",
                 (unsigned)info.size_user/1024,(unsigned)info.size_cdram/1024);
        status(text);
    }
    if(info.size_user<(int)(ACP_GPU_MAIN_BYTES+32u*1024*1024)||
       info.size_cdram<(int)(ACP_GPU_CDRAM_BYTES+16u*1024*1024)){
        if(status)status("VitaGL initialization failed: insufficient reserved-memory headroom");
        return -1;
    }
    vglUseExtraMem(GL_FALSE);
    vglUseTripleBuffering(GL_FALSE);
    vglSetCircularPoolSize(4u*1024*1024);
    /* This API returns a resolution-clamping flag, not initialization success.
     * GPU pools are physical reservations, not an estimate of live textures. */
    (void)vglInitWithCustomSizes(0,960,544,ACP_GPU_MAIN_BYTES,ACP_GPU_CDRAM_BYTES,
                                0,0,SCE_GXM_MULTISAMPLE_NONE);
    if(vglMemTotal(VGL_MEM_VRAM)!=ACP_GPU_CDRAM_BYTES||!glGetString(GL_VERSION)){
        if(status)status("VitaGL initialization failed: GPU pools or GL context unavailable");
        return -1;
    }
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
    vglSetDisplayCallback(display_fps);
    (void)eglSwapInterval(NULL,2);
    if(status)status("VitaGL initialized");
    return 0;
}
static void swap(void){
    static int first=1;
    static uint64_t previous,engine_us,swap_us;
    static unsigned samples,frame_times[120],last_draws;
    uint64_t begin=sceKernelGetProcessTimeWide();
    if(previous)engine_us+=begin-previous;
    acp_capture_frame(acp_presented_frames+1);
    /* A dialog (Java popup or the trophy setup) needs sceCommonDialogUpdate. */
    int dialog=acp_popup_vita_frame();
    vglSwapBuffers(dialog||acp_trophy_dialog_frame()?GL_TRUE:GL_FALSE);
    uint64_t end=sceKernelGetProcessTimeWide();
    if(previous){
        swap_us+=end-begin;
        uint64_t elapsed=end-previous;
        frame_times[samples]=elapsed>0xffffffffu?0xffffffffu:(unsigned)elapsed;
        if(++samples==120){
            /* Fixed storage, one bounded sort per reporting window. These are
             * presented frame times, not an estimate of hardware GPU time. */
            unsigned late=0;
            for(unsigned i=0;i<samples;i++){
                late+=frame_times[i]>40000;
                unsigned value=frame_times[i],j=i;
                while(j && frame_times[j-1]>value){frame_times[j]=frame_times[j-1];--j;}
                frame_times[j]=value;
            }
            unsigned fps=__atomic_load_n(&display_rate,__ATOMIC_RELAXED);
            acp_log("Frame timing mean: game=%u us present=%u us p95=%u us max=%u us over40ms=%u/120 draws/frame=%u display=%u.%u FPS",
                (unsigned)(engine_us/samples),(unsigned)(swap_us/samples),frame_times[113],frame_times[119],late,
                (acp_draw_calls-last_draws)/samples,fps/10,fps%10);
            acp_log("Draw sample (1/16 frames): calls=%u total=%u us max=%u us",
                    draw_sample_count,draw_sample_us,draw_sample_max);
            draw_sample_count=draw_sample_us=draw_sample_max=0;
            acp_log("Render wait sample (1/16): clear=%u us n=%u max=%u flush=%u us n=%u max=%u readback=%u us n=%u max=%u; GPU=%d MHz",
                render_waits[0].total,render_waits[0].count,render_waits[0].maximum,
                render_waits[1].total,render_waits[1].count,render_waits[1].maximum,
                render_waits[2].total,render_waits[2].count,render_waits[2].maximum,scePowerGetGpuClockFrequency());
            memset(render_waits,0,sizeof(render_waits));
            acp_gc_report();
            acp_frame_profile_report();
            acp_native_heap_report();
            acp_texture_flip_report();
            acp_contact_reports_log();
            acp_animation_stats_log();
            acp_engine_timing_report();
            acp_matrix_report();
            acp_thread_timing_report();
            acp_lua_load_report();
            last_draws=acp_draw_calls;
            engine_us=swap_us=0;samples=0;
        }
    }
    previous=end;
    unsigned frame=++acp_presented_frames;
    extern void acp_jni_frame_boundary(void);
    extern void acp_loading_boost_check(void);
    acp_jni_frame_boundary();acp_loading_boost_check();
    if(frame==120||frame==600||frame==2400){
        if(status){char text[128];snprintf(text,sizeof(text),"Render checkpoint: %u buffers submitted, %u draws",frame,acp_draw_calls);status(text);}
        acp_render_checkpoint();
    }
    if(first){first=0;if(status)status("First game buffer submitted to the display");}
}
static void interval(int n){(void)eglSwapInterval(NULL,n);}
static int valid_window(void *p){return p==window;}
static void fail(GLenum e){if(!pending_error)pending_error=e;}
static int current(void){
    if(acp_egl_current())return 1;
    fail(GL_INVALID_OPERATION);return 0;
}
static GLenum wrapped_glGetError(void){
    if(pending_error){GLenum e=pending_error;pending_error=0;return e;}
    return glGetError();
}
static const GLubyte *wrapped_glGetString(GLenum name){
    if(!current())return NULL;
    /* Suppress the desktop/ES3 and multisample extensions exposed by vitaGL.
     * geOesRenderer::InitContext selects paths using these strings. */
    if(name==GL_EXTENSIONS)return (const GLubyte *)extensions;
    if(name==GL_VERSION)return (const GLubyte *)"OpenGL ES 2.0 ACP VitaGL";
    return glGetString(name);
}
static void wrapped_glGetIntegerv(GLenum name,GLint *out){
    if(!current())return;
    if(!out){fail(GL_INVALID_VALUE);return;}
    if(name==GL_NUM_COMPRESSED_TEXTURE_FORMATS){*out=1;return;}
    if(name==GL_COMPRESSED_TEXTURE_FORMATS){*out=GL_ETC1_RGB8_OES;return;}
    glGetIntegerv(name,out);
}
static void wrapped_glShaderSource(GLuint shader,GLsizei count,const GLchar *const *parts,const GLint *lengths){
    if(!current())return;
    if(count<0||count>32||(count&&!parts)){fail(GL_INVALID_VALUE);return;}
    size_t total=0,part_lengths[32];
    for(int i=0;i<count;i++){
        if(!parts[i]){fail(GL_INVALID_VALUE);return;}
        size_t n=0;
        if(lengths&&lengths[i]>=0)n=(size_t)lengths[i];
        else while(n<=256u*1024&&parts[i][n])n++;
        if(n>256u*1024-total){fail(GL_OUT_OF_MEMORY);return;}
        total+=n;
        part_lengths[i]=n;
    }
    char *joined=malloc(total+1);
    if(!joined){fail(GL_OUT_OF_MEMORY);return;}
    size_t offset=0;
    for(int i=0;i<count;i++){
        memcpy(joined+offset,parts[i],part_lengths[i]);offset+=part_lengths[i];
    }
    joined[total]=0;
    acp_glsl_identifiers(joined,total);
    GLint type=0;glGetShaderiv(shader,GL_SHADER_TYPE,&type);
    if(type==GL_FRAGMENT_SHADER){
        char *filtered=acp_water_shader(joined);
        if(filtered){free(joined);joined=filtered;total=strlen(joined);acp_log("Ocean reflection footprint filter applied");}
    }
    acp_shader_trace(joined,shader,(unsigned)type,0);
    const GLchar *text=joined;GLint bytes=(GLint)total;
    glShaderSource(shader,1,&text,&bytes);
    free(joined);
}
static void report(GLuint id,int program){
    if(!logger)return;
    GLint status=0;
    char log[2048]={0};
    if(program){glGetProgramiv(id,GL_LINK_STATUS,&status);glGetProgramInfoLog(id,sizeof(log),NULL,log);}
    else {glGetShaderiv(id,GL_COMPILE_STATUS,&status);glGetShaderInfoLog(id,sizeof(log),NULL,log);}
    log[sizeof(log)-1]=0;
    logger(id,program,status,log);
}
static void wrapped_glCompileShader(GLuint s){if(current()){glCompileShader(s);report(s,0);}}
static void wrapped_glClear(GLbitfield mask){
    int sample=!(acp_presented_frames&15u);
    uint64_t start=sample?sceKernelGetProcessTimeWide():0;
    glClear(mask);if(sample)render_wait_end(0,start);
}
static void wrapped_glFlush(void){
    int sample=!(acp_presented_frames&15u);
    uint64_t start=sample?sceKernelGetProcessTimeWide():0;
    glFlush();if(sample)render_wait_end(1,start);
}
static void wrapped_glReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,void *p){
    int sample=!(acp_presented_frames&15u);
    uint64_t start=sample?sceKernelGetProcessTimeWide():0;
    glReadPixels(x,y,w,h,format,type,p);if(sample)render_wait_end(2,start);
}
static void draw_sample_end(uint32_t begin){
    unsigned elapsed=(uint32_t)sceKernelGetProcessTimeWide()-begin;
    draw_sample_count++;draw_sample_us+=elapsed;if(elapsed>draw_sample_max)draw_sample_max=elapsed;
}
static void wrapped_glDrawElements(GLenum mode,GLsizei count,GLenum type,const void *indices){
    acp_vitagl_water_trace();
    acp_vitagl_water_compare(1);
    int sample=!(acp_presented_frames&15u);
    uint32_t begin=sample?(uint32_t)sceKernelGetProcessTimeWide():0;
    ++acp_draw_calls;glDrawElements(mode,count,type,indices);
    if(sample)draw_sample_end(begin);
    acp_vitagl_water_compare(0);
}
static void wrapped_glDrawArrays(GLenum mode,GLint first,GLsizei count){
    acp_vitagl_water_trace();
    acp_vitagl_water_compare(1);
    int sample=!(acp_presented_frames&15u);
    uint32_t begin=sample?(uint32_t)sceKernelGetProcessTimeWide():0;
    ++acp_draw_calls;glDrawArrays(mode,first,count);
    if(sample)draw_sample_end(begin);
    acp_vitagl_water_compare(0);
}
/* geOesShader::~geOesShader detaches its two shaders immediately before
 * deleting the program, then deletes both shaders (ELF 0x77ef15).
 * vitaGL retains shader references for its linked executable and releases
 * them in glDeleteProgram. Record logical detachment and let that destructor
 * release the retained references; never recycle a shader while GXM uses it.
 * This adapter supports the game's destruction sequence, not arbitrary
 * detach-and-relink. Reject that sequence explicitly if it is encountered. */
static struct {GLuint program,shader[2];} detach[16];
static int detachment(GLuint p){
    for(unsigned i=0;i<16;i++)if(detach[i].program==p)return (int)i;
    return -1;
}
static void wrapped_glDetachShader(GLuint p,GLuint s){
    if(!current())return;
    if(!p||!s||!glIsProgram(p)){fail(GL_INVALID_VALUE);return;}
    GLuint attached[2]={0};GLsizei n=0;
    glGetAttachedShaders(p,2,&n,attached);
    if((n<1||attached[0]!=s)&&(n<2||attached[1]!=s)){fail(GL_INVALID_OPERATION);return;}
    int slot=detachment(p);
    if(slot<0)for(unsigned i=0;i<16;i++)if(!detach[i].program){slot=(int)i;break;}
    if(slot<0){fail(GL_OUT_OF_MEMORY);return;}
    if(detach[slot].shader[0]==s||detach[slot].shader[1]==s){fail(GL_INVALID_OPERATION);return;}
    detach[slot].program=p;
    if(!detach[slot].shader[0])detach[slot].shader[0]=s;else detach[slot].shader[1]=s;
}
static void wrapped_glDeleteProgram(GLuint p){
    if(!current())return;
    if(!p)return;
    if(!glIsProgram(p)){fail(GL_INVALID_VALUE);return;}
    acp_vitagl_water_forget(p);
    glDeleteProgram(p);
    int slot=detachment(p);
    if(slot>=0)memset(&detach[slot],0,sizeof(detach[slot]));
}
static void wrapped_glLinkProgram(GLuint p){
    if(!current())return;
    if(detachment(p)>=0){fail(GL_INVALID_OPERATION);return;}
    acp_vitagl_water_forget(p);
    glLinkProgram(p);
    /* POSTPONED mode compiles the actual vertex/fragment pair during link. */
    GLuint stages[2]={0};GLsizei count=0;
    glGetAttachedShaders(p,2,&count,stages);
    for(GLsizei i=0;i<count;i++)report(stages[i],0);
    if(acp_shader_trace_enabled())acp_log("Shader pair program=%u stages=%u,%u",p,stages[0],stages[1]);
    report(p,1);
}
static void wrapped_glFramebufferTexture2D(GLenum target,GLenum attachment,GLenum tex_target,GLuint texture,GLint level){
    static unsigned traced;
    glFramebufferTexture2D(target,attachment,tex_target,texture,level);
    if(traced<512&&acp_shader_trace_enabled()){
        GLint fb=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);
        acp_log("FBO attach fb=%d attachment=%x texture=%u target=%x level=%d",fb,attachment,texture,tex_target,level);++traced;
    }
}
static void wrapped_glTexImage2D(GLenum target,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void *data){
    static unsigned traced;
    glTexImage2D(target,level,internal,w,h,border,format,type,data);
    if(level==0&&traced<1024&&acp_shader_trace_enabled()){
        GLint tex=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex);
        acp_log("Texture image id=%d target=%x level=%d %dx%d internal=%x format=%x type=%x data=%s",tex,target,level,w,h,internal,format,type,data?"pixels":"null");++traced;
    }
}
static void wrapped_glCompressedTexImage2D(GLenum target,GLint level,GLenum format,
        GLsizei width,GLsizei height,GLint border,GLsizei bytes,const void *data){
    if(!current())return;
    static unsigned rejected;
    if(target!=GL_TEXTURE_2D||format!=GL_ETC1_RGB8_OES){
        if(rejected++<64)acp_log("Rejected compressed texture: target=%x format=%x level=%d %dx%d bytes=%d",target,format,level,width,height,bytes);
        fail(GL_INVALID_ENUM);return;
    }
    unsigned expected=acp_etc1_image_bytes(width,height);
    if(!expected||level<0||level>12||border||bytes!=(GLsizei)expected){
        if(rejected++<64)acp_log("Rejected compressed texture dimensions: level=%d %dx%d border=%d bytes=%d",level,width,height,border,bytes);
        fail(GL_INVALID_VALUE);return;
    }
    glCompressedTexImage2D(target,level,format,width,height,border,bytes,data);
}
static void wrapped_glCompressedTexSubImage2D(GLenum target,GLint level,GLint x,GLint y,
        GLsizei w,GLsizei h,GLenum format,GLsizei bytes,const void *data){
    (void)target;(void)level;(void)x;(void)y;(void)w;(void)h;(void)format;(void)bytes;(void)data;
    /* ETC1 does not permit compressed subimage updates. vitaGL has no such
     * entry point; a successful no-op would corrupt the engine's texture state. */
    if(current())fail(GL_INVALID_OPERATION);
}
static uintptr_t resolve_gl(const char *name){
    if(!name)return 0;
#define WRAP(n) if(!strcmp(name,#n))return (uintptr_t)&wrapped_##n
    WRAP(glGetError);WRAP(glGetString);WRAP(glGetIntegerv);WRAP(glShaderSource);
    WRAP(glCompileShader);WRAP(glLinkProgram);WRAP(glCompressedTexImage2D);
    WRAP(glCompressedTexSubImage2D);
    WRAP(glDetachShader);WRAP(glDeleteProgram);
    WRAP(glDrawElements);WRAP(glDrawArrays);
    WRAP(glClear);WRAP(glFlush);WRAP(glReadPixels);
    WRAP(glFramebufferTexture2D);WRAP(glTexImage2D);
#undef WRAP
    /* Static allowlist: dynamic lookup cannot accidentally expose desktop,
     * ES3, or vitaGL's memory-allocation/control functions to the engine. */
#define ACP_GL(n) if(!strcmp(name,#n))return (uintptr_t)&n;
#include "acp_gl_core.inc"
#undef ACP_GL
    return 0;
}
int acp_graphics_install(void *native_window){
    if(!native_window||window)return -1;
    acp_egl_backend b={thread_id,initialize,swap,interval,valid_window,resolve_gl};
    if(acp_egl_install(&b))return -1;
    window=native_window;return 0;
}
uintptr_t acp_graphics_resolve(const char *name){return acp_egl_resolve(name);}
void acp_graphics_set_log(acp_shader_log f){logger=f;}
