#ifndef ACP_GRAPHICS_H
#define ACP_GRAPHICS_H
#include <stddef.h>
#include <stdint.h>
/* Call before constructors; actual GPU initialization occurs on eglInitialize's
 * render thread. Returns both EGL and the restricted GLES2 dispatch. */
int acp_graphics_install(void *native_window);
uintptr_t acp_graphics_resolve(const char *name);
typedef void (*acp_shader_log)(unsigned id,int program,int success,const char *log);
void acp_graphics_set_log(acp_shader_log);
void acp_graphics_set_status(void (*)(const char *));
#define ACP_GPU_MAIN_BYTES (32u*1024*1024)
#define ACP_GPU_CDRAM_BYTES (96u*1024*1024)
/* ETC1 stores eight bytes per 4x4 block, including partial edge blocks. */
static inline unsigned acp_etc1_image_bytes(int width,int height){
    if(width<=0||height<=0||width>4096||height>4096)return 0;
    unsigned columns=((unsigned)width+3)/4;
    unsigned rows=((unsigned)height+3)/4;
    return columns*rows*8;
}
#endif
