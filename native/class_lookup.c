#include "acp_native.h"
#include "acp_class_lookup.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
#ifdef ACP_CLASS_COUNTERS
void acp_class_lookup_report(void){
    unsigned count[3];acp_class_lookup_counts(count);
    if(count[0]||count[1]||count[2])acp_log("Class lookup (1/256 frames): ordinary=%u function-getter=%u fallback=%u",count[0],count[1],count[2]);
}
#endif
int acp_class_lookup_install(so_module *m){
    /* indexFactory's PC-relative literal supplies its C closure callback.
     * Preserve the original callback intact for all non-ordinary reads. */
    static const uint8_t factory[]={0x03,0x49,0x02,0x22,0x08,0xb5,0x79,0x44,
        0x91,0xf1,0x1e,0xfa,0x01,0x20,0x08,0xbd,0x73,0,0,0};
    static const uint8_t index[]={0x38,0xb5,0x02,0x21,0x04,0x46,0x90,0xf1,0x6d,0xfd};
    if(memcmp((void *)(m->load_addr+0x52a328),factory,sizeof(factory)) ||
       memcmp((void *)(m->load_addr+0x52a3a4),index,sizeof(index)))return -1;
    acp_class_lookup_original((void *)(m->load_addr+0x52a3a5),(void *)(m->load_addr+0x6bc0b5));
    uint32_t delta=(uint32_t)(uintptr_t)acp_class_index-(uint32_t)(m->load_addr+0x52a332);
    if(kuKernelCpuUnrestrictedMemcpy((void *)(m->load_addr+0x52a338),&delta,4)<0)return -1;
    acp_log("Class properties: native string lookup and getter argument setup; original VM calls, setters and fallback retained");
    return 0;
}
