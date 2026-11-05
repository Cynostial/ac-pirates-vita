/* The pinned renderer dirties eleven derived matrices even when its world
 * matrix is unchanged. Preserve those caches on exact byte equality. No scene
 * objects, shader values, frame snapshots or GPU buffers are retained here. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
static void (*base_set_world)(void *,const void *);
static unsigned world_calls,world_reused;
static void set_world(void *interface,const void *matrix){
    unsigned char *renderer=*(unsigned char **)interface;
    void (**vtable)(void *,const void *)=*(void (***)(void *,const void *))renderer;
    void (*setter)(void *,const void *)=vtable[0x10c/4];
    ++world_calls;
    if(setter==base_set_world&&!memcmp(renderer+0x10c,matrix,64)){
        ++world_reused;return;
    }
    /* Preserve custom virtual implementations and all changed-value writes.
     * In particular, never clear a dirty bit set by a view/projection change. */
    setter(renderer,matrix);
}
void acp_matrix_report(void){
    acp_log("World matrix reuse: calls=%u unchanged=%u",world_calls,world_reused);
    world_calls=world_reused=0;
}
int acp_matrix_install(so_module *m){
    static const unsigned char guard[8]={0x00,0x68,0x70,0xb4,0x04,0x68,0x2a,0x4a};
    static const unsigned char base_guard[8]={0xca,0x68,0x01,0x23,0x70,0xb4,0x0e,0x68};
    uintptr_t site=m->load_addr+0x787f84;
    if(memcmp((void *)site,guard,8)||memcmp((void *)(m->load_addr+0x77bd2c),base_guard,8))return -1;
    base_set_world=(void *)(m->load_addr+0x77bd2d);
    unsigned char jump[8];if(acp_game_patch_build((uintptr_t)set_world,jump))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)site,jump,8)<0)return -1;
    acp_log("World matrix: preserve derived caches for identical transforms");return 0;
}
