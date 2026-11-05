/* The shipped ARM engine retains string construction/destruction for disabled
 * render profiling scopes. None of these temporary strings escape, and every
 * scope is destroyed before the first rendering operation. Skip only those
 * dead scopes, preserving each function's prologue and live register setup.
 * Addresses/guards are for the loader's SHA-256-pinned 2.9.1 ARMv7 binary. */
#include "acp_native.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>

static const struct {uint32_t site,continuation;uint8_t guard[8];} scopes[]={
    {0x79ff7a,0x79ffb6,{0xc0,0x68,0xef,0xf7,0xd0,0xfe,0x05,0xae}}, /* SubMeshEntity */
    {0x793f78,0x793f90,{0x11,0xf3,0xa4,0xe4,0x32,0x99,0xdf,0xf8}}, /* RenderGroup */
    {0x7852b4,0x7852ea,{0x18,0x46,0x09,0xf0,0xc1,0xf8,0x01,0xaa}}, /* Compute */
    {0x791c0c,0x791c42,{0xfc,0xf7,0x16,0xfc,0x04,0xaa,0x01,0x46}}, /* PostProcess; retain virtual GetMaterial */
    {0x797008,0x797038,{0x08,0xa8,0x0e,0xf3,0x5c,0xe4,0x09,0xaf}}, /* SceneRenderer */
};
static uintptr_t image;

/* Used at startup and by the explicitly enabled emulator comparison mailbox.
 * The mailbox runs on the game thread between frames, outside these functions. */
int acp_render_scopes_mode(unsigned optimized){
    if(!image||optimized>1)return -1;
    for(unsigned i=0;i<sizeof(scopes)/sizeof(scopes[0]);i++){
        uintptr_t site=image+scopes[i].site;
        uint16_t branch=(uint16_t)(0xe000u|((scopes[i].continuation-scopes[i].site-4u)>>1));
        const void *bytes=optimized?(const void *)&branch:(const void *)scopes[i].guard;
        if(kuKernelCpuUnrestrictedMemcpy((void *)site,bytes,2)<0)return -1;
        kuKernelFlushCaches((void *)site,2);
    }
    return 0;
}
int acp_render_scopes_install(so_module *m){
    for(unsigned i=0;i<sizeof(scopes)/sizeof(scopes[0]);i++){
        if(memcmp((void *)(m->load_addr+scopes[i].site),scopes[i].guard,8))return -1;
        uint32_t d=scopes[i].continuation-scopes[i].site-4u;
        if((d&1)||d>2046)return -1;
    }
    image=m->load_addr;
    if(acp_render_scopes_mode(1))return -1;
    acp_log("Render scopes: removed unused debug-label string lifetimes at five verified sites");
    return 0;
}
