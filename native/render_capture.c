/* Optional developer readback of an actual game frame. Disabled unless the
 * data directory contains capture-frame.flag; never substitutes a scene. */
#include "acp_native.h"
#include "acp_platform.h"
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
extern unsigned acp_emulator_capture_request(void);
extern void acp_game_memory_report(void);
void acp_capture_frame(unsigned frame){
    static int enabled=-1;
    if(enabled<0){
        FILE *flag=fopen(ACP_DATA "capture-frame.flag","rb");
        enabled=flag!=NULL;if(flag)fclose(flag);
    }
    unsigned request=acp_emulator_capture_request();
    if(!request&&(!enabled||(frame!=600&&frame!=2400)))return;
    GLint framebuffer=0,viewport[4]={0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    glGetIntegerv(GL_VIEWPORT,viewport);
    if(framebuffer||viewport[2]!=960||viewport[3]!=544){
        acp_log("Frame %u readback deferred: framebuffer=%d viewport=%d,%d,%d,%d",
                frame,framebuffer,viewport[0],viewport[1],viewport[2],viewport[3]);
        return;
    }
    unsigned char *pixels=malloc(960u*544*4);
    if(!pixels){acp_log("Frame %u readback allocation failed",frame);return;}
    glReadPixels(0,0,960,544,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    char path[128];snprintf(path,sizeof(path),ACP_DATA "frame-%u.rgba",frame);
    if(request)snprintf(path,sizeof(path),ACP_DATA "api/frame-%u.rgba",request);
    FILE *out=fopen(path,"wb");
    size_t written=out?fwrite(pixels,1,960u*544*4,out):0;
    if(out)fclose(out);
    unsigned colored=0;
    for(unsigned i=0;i<960u*544;i++)colored+=pixels[i*4]||pixels[i*4+1]||pixels[i*4+2];
    acp_log("Frame %u readback: %u bytes, %u nonblack pixels",frame,(unsigned)written,colored);
    if(request){
        acp_game_memory_report();
        size_t main_free=vglMemFree(VGL_MEM_RAM),vram_free=vglMemFree(VGL_MEM_VRAM);
        if(main_free<=vglMemTotal(VGL_MEM_RAM)&&vram_free<=vglMemTotal(VGL_MEM_VRAM))
            acp_log("GPU pools free: main %u KiB, CDRAM %u KiB",(unsigned)main_free/1024,(unsigned)vram_free/1024);
        else acp_log("GPU free-space query unavailable on this runtime");
        free(pixels);return;
    }
    /* RGB conversion takes VitaGL's GXM transfer path, independently of its
     * direct CPU RGBA readback. Useful for checking emulator surface sync. */
    glReadPixels(0,0,960,544,GL_RGB,GL_UNSIGNED_BYTE,pixels);
    snprintf(path,sizeof(path),ACP_DATA "frame-%u.rgb",frame);
    out=fopen(path,"wb");written=out?fwrite(pixels,1,960u*544*3,out):0;
    if(out)fclose(out);
    colored=0;
    for(unsigned i=0;i<960u*544;i++)colored+=pixels[i*3]||pixels[i*3+1]||pixels[i*3+2];
    acp_log("Frame %u GPU readback: %u bytes, %u nonblack pixels",frame,(unsigned)written,colored);
    free(pixels);
}
