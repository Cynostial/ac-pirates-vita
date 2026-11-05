/* The engine's component setters invalidate even identical values. Reuse a
 * previously calculated matrix only when both inputs AND current output match.
 * A recycled object address cannot cause a hit with a different transform.
 * Pending bounds flags are never cleared. This fixed table owns no objects. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
typedef struct {uintptr_t key;uint32_t fpscr;uint8_t inputs[40],matrix[64];} Entry;
static Entry entries[512];
static unsigned mesh_cache_lock;
static unsigned dirty,reused,conflicts;
static void *(*calculate)(void *);
/* Rounding, flush/default-NaN modes and cumulative exception state. FPSCR
 * condition flags are untouched by the original arithmetic. Trap-enabled
 * arithmetic always executes the original calculation. */
static uint32_t floating_state(void){uint32_t s;__asm__ volatile("vmrs %0,fpscr":"=r"(s));return s;}
static void *get_mesh_transform(void *object){
    uint8_t *mesh=object;
    if(!mesh[0xd8])return mesh+0x98;
    __atomic_fetch_add(&dirty,1,__ATOMIC_RELAXED);
    uint32_t fp=floating_state();
    if((fp&0x9f00u)||__atomic_exchange_n(&mesh_cache_lock,1,__ATOMIC_ACQUIRE)){
        __atomic_fetch_add(&conflicts,1,__ATOMIC_RELAXED);return calculate(object);
    }
    uintptr_t key=(uintptr_t)object;
    Entry *entry=entries+(((key>>4)^(key>>13))&511u);
    if(entry->key==key && entry->fpscr==(fp&0x07c0009fu) &&
       !memcmp(entry->inputs,mesh+0x70,40) && !memcmp(entry->matrix,mesh+0x98,64)){
        mesh[0xd8]=0;
        __atomic_fetch_add(&reused,1,__ATOMIC_RELAXED);
    }else{
        calculate(object);
        entry->key=key;entry->fpscr=floating_state()&0x07c0009fu;
        memcpy(entry->inputs,mesh+0x70,40);memcpy(entry->matrix,mesh+0x98,64);
    }
    __atomic_store_n(&mesh_cache_lock,0,__ATOMIC_RELEASE);
    return mesh+0x98;
}
void acp_mesh_transform_report(void){
    unsigned d=__atomic_exchange_n(&dirty,0,__ATOMIC_RELAXED);
    unsigned r=__atomic_exchange_n(&reused,0,__ATOMIC_RELAXED);
    unsigned c=__atomic_exchange_n(&conflicts,0,__ATOMIC_RELAXED);
    acp_log("Mesh transform reuse: dirty=%u unchanged=%u fallback=%u storage=%u bytes",d,r,c,(unsigned)sizeof(entries));
}
int acp_mesh_transform_install(so_module *m){
    static const uint8_t guard[10]={0x90,0xf8,0xd8,0x30,0x00,0x2b,0x00,0xf0,0x81,0x80};
    uintptr_t site=m->load_addr+0x78fe48;
    if(memcmp((void *)site,guard,sizeof(guard)))return -1;
    /* The original dirty body begins after its flag check and branch. It uses
     * only r0 as input, has no skipped stack setup, and returns the matrix. */
    calculate=(void *)(site+11);
    uint8_t jump[8];if(acp_game_patch_build((uint32_t)(uintptr_t)get_mesh_transform,jump))return -1;
    if(kuKernelCpuUnrestrictedMemcpy((void *)site,jump,8)<0)return -1;
    acp_log("Mesh transforms: exact deferred-matrix reuse, 512 bounded entries");
    return 0;
}
