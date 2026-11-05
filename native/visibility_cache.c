/* Reuse exact box/plane classifications, not visibility lists or scene objects.
 * Moving cameras use the original arithmetic. A fixed camera can reuse classifications
 * across frames, while changed bounds still run the original arithmetic.
 * Hidden meshes are omitted before box tests: the pinned geSubMeshEntity::IsVisible
 * checks GetVisible before its sole use of GetCullingResult (ELF 0x79ff5c).
 * Shadow visibility also checks GetVisible and does not use the culling result.
 * Live scene traversal is retained; no visibility list or entity lifetime is cached. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>

typedef struct {uintptr_t root;unsigned epoch,used;uint32_t planes[24];} View;
typedef struct {uintptr_t box;uint32_t epoch,scale,control,box_bits[6],fp,result;} Entry;
static View views[16];
static Entry entries[4096];
static unsigned serial,stamp,busy,hits,misses,bypassed,hidden;
/* Only the opt-in local emulator mailbox can change this comparison switch. */
volatile unsigned acp_visibility_cache_mode=1;
static void (*original_query)(void *,void *,const void *);
static void (*original_append)(void *,const uintptr_t *);
static unsigned (*original_test)(void *,const void *,const void *,uint32_t);
static uint32_t fp_read(void){uint32_t v;__asm__ volatile("vmrs %0,fpscr":"=r"(v));return v;}
static void fp_write(uint32_t v){__asm__ volatile("vmsr fpscr,%0"::"r"(v));}

static unsigned classify(View *view,void *node,const void *box,const void *planes,uint32_t scale){
    if(!view)return original_test(node,box,planes,scale);
    uint32_t fp=fp_read();
    if(fp&0x9f00u)return original_test(node,box,planes,scale);
    uintptr_t key=(uintptr_t)box;
    Entry *e=entries+(((key>>4)^(key>>13)^(view->epoch*31u))&4095u);
    if(e->box==key && e->epoch==view->epoch && e->scale==scale &&
       e->control==(fp&0x07c0009fu) && !memcmp(e->box_bits,box,24)){
        hits++;
        /* Replay comparison flags and sticky exceptions from the same inputs. */
        fp_write((fp&~0xf000009fu)|(e->fp&0xf000009fu)|(fp&0x9fu));
        return e->result;
    }
    misses++;
    unsigned result=original_test(node,box,planes,scale);
    e->fp=fp_read();e->box=key;e->epoch=view->epoch;e->scale=scale;
    e->control=fp&0x07c0009fu;memcpy(e->box_bits,box,24);e->result=result;
    return result;
}
typedef struct {uint8_t box[24],header[8];void *children[8];uintptr_t *begin,*end,*capacity;} Node;
typedef struct {uintptr_t *begin,*end,*capacity;} Vector;
_Static_assert(offsetof(Node,children)==0x20 && offsetof(Node,begin)==0x40,"Pinned ARM node layout");
static void append(Vector *output,uintptr_t mesh){
    if(output->end==output->capacity)original_append(output,&mesh);
    else *output->end++=mesh;
}
static void visible_all(Node *node,Vector *output){
    for(unsigned i=0;i<(unsigned)(node->end-node->begin);i++){
        const uint8_t *box=(void *)node->begin[i];
        if(!box[24])continue;
        if(!box[0x60]){hidden++;continue;}
        append(output,node->begin[i]-4);
    }
    for(unsigned i=0;i<8;i++)if(node->children[i])visible_all(node->children[i],output);
}
static void visit(View *view,Node *node,Vector *output,const void *planes){
    unsigned result=classify(view,node,node,planes,0x40000000u);
    if(result==0){visible_all(node,output);return;}
    if(result!=1)return;
    for(unsigned i=0;i<(unsigned)(node->end-node->begin);i++){
        const uint8_t *box=(void *)node->begin[i];
        if(!box[24])continue;
        if(!box[0x60]){hidden++;continue;}
        if(classify(view,node,box,planes,0x3f800000u)!=2){
            append(output,node->begin[i]-4);
        }
    }
    for(unsigned i=0;i<8;i++)if(node->children[i])visit(view,node->children[i],output,planes);
}
static void acp_visibility_query(void *node,void *output,const void *planes){
    if(!__atomic_load_n(&acp_visibility_cache_mode,__ATOMIC_RELAXED)||(fp_read()&0x9f00u)){
        original_query(node,output,planes);return;
    }
    if(__atomic_exchange_n(&busy,1,__ATOMIC_ACQUIRE)){
        original_query(node,output,planes);return;
    }
    View *view=NULL,*oldest=views;
    for(unsigned i=0;i<16;i++){
        if(views[i].root==(uintptr_t)node){view=views+i;break;}
        if(views[i].used<oldest->used)oldest=views+i;
    }
    if(!view)view=oldest;
    if(view->root==(uintptr_t)node && !memcmp(view->planes,planes,96)){
        view->used=++stamp;
        visit(view,node,output,planes);
    }else{
        if(!++serial){memset(entries,0,sizeof(entries));memset(views,0,sizeof(views));serial=1;}
        view->root=(uintptr_t)node;view->epoch=serial;memcpy(view->planes,planes,96);bypassed++;
        view->used=++stamp;
        visit(NULL,node,output,planes);
    }
    __atomic_store_n(&busy,0,__ATOMIC_RELEASE);
}
void acp_visibility_cache_report(void){
    acp_log("Visibility reuse: hits=%u misses=%u changed-view=%u hidden-omitted=%u storage=%u bytes",hits,misses,bypassed,hidden,(unsigned)(sizeof(views)+sizeof(entries)));
    hits=misses=bypassed=hidden=0;
}
int acp_visibility_cache_install(so_module *m){
    const struct {uint32_t site;uint8_t guard[4];uintptr_t target;} hooks[]={
        {0x79612a,{0xff,0xf7,0x39,0xfe},(uintptr_t)acp_visibility_query},
    };
    for(unsigned i=0;i<1;i++)if(memcmp((void *)(m->load_addr+hooks[i].site),hooks[i].guard,4))return -1;
    original_query=(void *)(m->load_addr+0x795da1);original_test=(void *)(m->load_addr+0x79532d);
    original_append=(void *)(m->load_addr+0x795bc5);
    for(unsigned i=0;i<1;i++){
        uintptr_t site=m->load_addr+hooks[i].site,stub=(m->patch_head+3u)&~3u;
        if(stub<m->patch_base||stub+8>m->patch_base+m->patch_size)return -1;
        int32_t offset=(int32_t)(stub-site-4);
        if(offset<-(1<<24)||offset>=(1<<24)||(offset&1))return -1;
        uint32_t bits=(uint32_t)offset,s=(bits>>24)&1;
        uint16_t branch[2]={(uint16_t)(0xf000|(s<<10)|((bits>>12)&1023)),
            (uint16_t)(0xd000|((1^((bits>>23)&1)^s)<<13)|((1^((bits>>22)&1)^s)<<11)|((bits>>1)&2047))};
        uint8_t jump[8];if(acp_game_patch_build((uint32_t)hooks[i].target,jump))return -1;
        if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,8)<0)return -1;
        kuKernelFlushCaches((void *)stub,8);
        if(kuKernelCpuUnrestrictedMemcpy((void *)site,branch,4)<0)return -1;
        m->patch_head=stub+8;
    }
    acp_log("Visibility: early hidden-mesh rejection, exact fixed-view reuse, original moving-view arithmetic");return 0;
}
