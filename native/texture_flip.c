/* geOesTexture::SetData uploads a vertically flipped copy of the caller's
 * texture: new[] the whole texture, geFlipTextureVertically per slice, then
 * glTex(Sub)Image/glCompressedTex(Sub)Image from the copy and delete[] it.
 * A 2048x2048 texture needs a 16 MiB copy next to its 16 MiB decoded image;
 * after a temple run the fragmented game heap could not place it and
 * std::bad_alloc closed the game. For textures of IN_PLACE_BYTES or more in
 * our heaps the copy is now the caller's buffer itself: each slice is flipped
 * in place (4-row bands swapped through a small buffer; 4 rows are one block
 * row of the compressed formats the engine flips) and flipped back when the
 * engine releases the "copy", so the caller's data is unchanged afterwards. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <stdint.h>
#include <string.h>

#define IN_PLACE_BYTES (1024u*1024u)
#define NEW_ARRAY 0xe47df4u      /* operator new[] (ARM) */
#define DELETE_ARRAY 0xe45f70u   /* operator delete[] (ARM) */
#define FLIP 0x780bd9u           /* geFlipTextureVertically (Thumb) */
#define MAX_SLICES 64u

typedef void *(*alloc_fn)(size_t);
typedef void (*release_fn)(void *);
typedef void (*flip_fn)(unsigned,unsigned,unsigned,unsigned char *,const unsigned char *,unsigned);
/* Global: the naked entry below jumps through it. */
alloc_fn acp_texture_flip_engine_new;
static release_fn engine_delete;
static flip_fn engine_flip;
static volatile int busy;
static struct {
    unsigned char *data;unsigned count,fallbacks;
    struct {unsigned w,h,format,bytes;unsigned char *p;} slice[MAX_SLICES];
} job;
static unsigned char band_buffer[64*1024];
static struct {unsigned textures,slices,fallbacks;unsigned long long bytes;} stats;

/* Formats the engine copies without flipping (geFlipTextureVertically). */
static int copied_only(unsigned format){
    return (format>=23&&format<=27)||(format>=30&&format<=35)||(format>=39&&format<=42);
}
static int compressed(unsigned format){return format>=20&&format<=22;}
/* Flipping is its own inverse: the same call flips back. */
static int flip_in_place(unsigned w,unsigned h,unsigned format,unsigned char *p,unsigned bytes){
    if(copied_only(format)||h<2)return 0;
    unsigned rows=h%4==0?4:compressed(format)?0:1;
    if(!rows||bytes%(h/rows))return -1;
    unsigned bands=h/rows;size_t band=bytes/bands;
    unsigned char *tmp=band<=sizeof(band_buffer)?band_buffer:acp_malloc(band);
    if(!tmp)return -1;
    for(unsigned i=0;i<bands/2;i++){
        unsigned char *a=p+(size_t)i*band,*b=p+(size_t)(bands-1-i)*band;
        engine_flip(w,rows,format,tmp,b,(unsigned)band);
        engine_flip(w,rows,format,b,a,(unsigned)band);
        memcpy(a,tmp,band);
    }
    if(bands&1){
        unsigned char *m=p+(size_t)(bands/2)*band;
        engine_flip(w,rows,format,tmp,m,(unsigned)band);memcpy(m,tmp,band);
    }
    if(tmp!=band_buffer)acp_free(tmp);
    return 0;
}
/* A slice the band swap cannot handle (compressed, height not a multiple of
 * 4): flip through an ordinary copy of that slice. acp_malloc never throws;
 * without memory the slice stays as it is in both directions. */
static void flip_copy(unsigned w,unsigned h,unsigned format,unsigned char *p,unsigned bytes){
    unsigned char *copy=acp_malloc(bytes);
    if(!copy)return;
    engine_flip(w,h,format,copy,p,bytes);memcpy(p,copy,bytes);acp_free(copy);
}
static void flip_slice_data(unsigned w,unsigned h,unsigned format,unsigned char *p,unsigned bytes){
    if(flip_in_place(w,h,format,p,bytes)){job.fallbacks++;flip_copy(w,h,format,p,bytes);}
}

/* Patched call sites. src is the engine's r5 (the caller's data). */
void *acp_texture_flip_take(size_t bytes,unsigned char *src){
    if(bytes>=IN_PLACE_BYTES&&src&&acp_memory_writable(src,bytes)&&!__sync_lock_test_and_set(&busy,1)){
        job.data=src;job.count=0;job.fallbacks=0;return src;
    }
    return NULL;
}
/* Otherwise operator new[] runs as a tail call: when it throws bad_alloc no
 * frame without unwind tables is on the stack. */
__attribute__((naked)) void acp_texture_flip_alloc_entry(void){
    __asm__ volatile(
        "push {r0, lr}\n\t"
        "mov r1, r5\n\t"
        "bl acp_texture_flip_take\n\t"
        "cbnz r0, 1f\n\t"
        "pop.w {r0, lr}\n\t"
        "movw r1, #:lower16:acp_texture_flip_engine_new\n\t"
        "movt r1, #:upper16:acp_texture_flip_engine_new\n\t"
        "ldr r1, [r1]\n\t"
        "bx r1\n"
        "1:\n\t"
        "add sp, sp, #4\n\t"
        "pop {pc}\n");
}
void acp_texture_flip_slice(unsigned w,unsigned h,unsigned format,unsigned char *dst,const unsigned char *src,unsigned bytes){
    if(!job.data||dst!=src){engine_flip(w,h,format,dst,src,bytes);return;}
    if(job.count<MAX_SLICES)job.slice[job.count++]=(__typeof__(job.slice[0])){w,h,format,bytes,dst};
    else job.fallbacks++;   /* not expected: flipped, but not restored on release */
    flip_slice_data(w,h,format,dst,bytes);
}
void acp_texture_flip_release(void *p){
    if(!job.data||p!=job.data){engine_delete(p);return;}
    for(unsigned i=0;i<job.count;i++)
        flip_slice_data(job.slice[i].w,job.slice[i].h,job.slice[i].format,job.slice[i].p,job.slice[i].bytes);
    stats.textures++;stats.slices+=job.count;stats.fallbacks+=job.fallbacks;
    for(unsigned i=0;i<job.count;i++)stats.bytes+=job.slice[i].bytes;
    job.data=NULL;job.count=0;
    __sync_lock_release(&busy);
}
void acp_texture_flip_report(void){
    if(stats.textures)acp_log("Texture flip in place: textures=%u slices=%u copied-slices=%u bytes=%llu",
        stats.textures,stats.slices,stats.fallbacks,stats.bytes);
    memset(&stats,0,sizeof(stats));
}

static int branch(uintptr_t site,uintptr_t stub,int link,uint16_t out[2]){
    int32_t delta=(int32_t)(stub-site-4);
    if(delta<-(1<<24)||delta>=(1<<24)||(delta&1))return -1;
    uint32_t bits=(uint32_t)delta,s=(bits>>24)&1;
    out[0]=(uint16_t)(0xf000|(s<<10)|((bits>>12)&1023));
    out[1]=(uint16_t)((link?0xd000:0x9000)|((1^((bits>>23)&1)^s)<<13)|((1^((bits>>22)&1)^s)<<11)|((bits>>1)&2047));
    return 0;
}
int acp_texture_flip_install(so_module *m){
    static const struct {uint32_t site;uint8_t guard[4];int link;void *target;} sites[]={
        {0x7818b8,{0xc6,0xf2,0x9c,0xe2},1,(void *)acp_texture_flip_alloc_entry},  /* blx operator new[] */
        {0x781916,{0xff,0xf7,0x5f,0xf9},1,(void *)acp_texture_flip_slice},        /* bl geFlipTextureVertically */
        {0x781950,{0x89,0xf1,0x18,0xb8},0,(void *)acp_texture_flip_release},      /* b.w delete[] (no upload) */
        {0x781978,{0x89,0xf1,0x04,0xb8},0,(void *)acp_texture_flip_release},      /* b.w delete[] (after upload) */
    };
    for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++)
        if(memcmp((void *)(m->load_addr+sites[i].site),sites[i].guard,4))return -1;
    acp_texture_flip_engine_new=(alloc_fn)(m->load_addr+NEW_ARRAY);
    engine_delete=(release_fn)(m->load_addr+DELETE_ARRAY);
    engine_flip=(flip_fn)(m->load_addr+FLIP);
    for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++){
        uintptr_t site=m->load_addr+sites[i].site,stub=(m->patch_head+3u)&~3u;
        if(stub<m->patch_base||stub+8>m->patch_base+m->patch_size)return -1;
        uint8_t jump[8];uint16_t call[2];
        if(acp_game_patch_build((uint32_t)(uintptr_t)sites[i].target,jump)||branch(site,stub,sites[i].link,call))return -1;
        if(kuKernelCpuUnrestrictedMemcpy((void *)stub,jump,8)<0)return -1;
        kuKernelFlushCaches((void *)stub,8);
        if(kuKernelCpuUnrestrictedMemcpy((void *)site,call,4)<0)return -1;
        kuKernelFlushCaches((void *)site,4);
        m->patch_head=stub+8;
    }
    return 0;
}
