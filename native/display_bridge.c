/* The Vita platform's Display.getRotation() returns ROTATION_0 unconditionally.
 * Replace the game's JNI query chain with that same value. Keep resolution and
 * window availability queries intact, including suspend/resume behavior. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
static int landscape_rotation(void){return 0;}
int acp_display_bridge_install(so_module *m){
    static const unsigned char guard[8]={0xf0,0xb5,0x85,0xb0,0x01,0xad,0x10,0x21};
    uintptr_t site=m->load_addr+0x52abb0;
    unsigned char jump[8];
    if(memcmp((void *)site,guard,sizeof(guard)))return -1;
    if(acp_game_patch_build((uintptr_t)landscape_rotation,jump))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)site,jump,sizeof(jump))<0)return -1;
    acp_log("Display rotation: native Vita landscape binding");
    return 0;
}
