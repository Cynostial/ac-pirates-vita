#include "acp_heap.h"
#include <string.h>
typedef struct {
    void *raw;
    acp_heap *owner;
    size_t bytes,charge,alignment;
} header;
/* Conservative metadata/rounding allowance in addition to our own header.
 * Physical fragmentation remains governed by the fixed mspace backing. */
#define BACKING_OVERHEAD 32u
static int sizes(acp_heap *h,size_t alignment,size_t bytes,size_t old,size_t *raw,size_t *charge){
    if(alignment<16)alignment=16;
    if((alignment&(alignment-1))||alignment>h->limit||bytes>h->limit)return 0;
    size_t pad=sizeof(header)+alignment-1;
    if(pad>SIZE_MAX-BACKING_OVERHEAD||bytes>SIZE_MAX-pad-BACKING_OVERHEAD)return 0;
    *raw=pad+(bytes?bytes:1);*charge=*raw+BACKING_OVERHEAD;
    return old<=h->used&&h->used-old<=h->limit&&*charge<=h->limit-(h->used-old);
}
static header *metadata(acp_heap *h,void *p){
    if(!p)return NULL;
    header *a=(header*)p-1;
    return a->owner==h&&a->charge<=h->used&&h->blocks?a:NULL;
}
static void *aligned(void *raw,size_t alignment){
    return (void*)(((uintptr_t)raw+sizeof(header)+alignment-1)&~(uintptr_t)(alignment-1));
}
static void commit(acp_heap *h,void *p,void *raw,size_t bytes,size_t charge,size_t alignment,size_t old){
    header *a=(header*)p-1;
    *a=(header){raw,h,bytes,charge,alignment};
    h->used=h->used-old+charge;if(h->used>h->peak)h->peak=h->used;
}
void *acp_heap_alloc(acp_heap *h,size_t alignment,size_t bytes){
    if(alignment<16)alignment=16;
    size_t raw_size,charge;
    if(!sizes(h,alignment,bytes,0,&raw_size,&charge)){h->failures++;return NULL;}
    void *raw=h->resize(h->context,NULL,raw_size);
    if(!raw){h->failures++;return NULL;}
    void *p=aligned(raw,alignment);commit(h,p,raw,bytes,charge,alignment,0);h->blocks++;return p;
}
int acp_heap_free(acp_heap *h,void *p){
    if(!p)return 0;
    header *a=metadata(h,p);if(!a)return -1;
    void *raw=a->raw;size_t charge=a->charge;a->owner=NULL;
    h->release(h->context,raw);h->used-=charge;h->blocks--;return 0;
}
size_t acp_heap_size(acp_heap *h,void *p){header *a=metadata(h,p);return a?a->bytes:0;}
void *acp_heap_resize(acp_heap *h,void *p,size_t bytes){
    if(!p)return acp_heap_alloc(h,16,bytes);
    if(!bytes){acp_heap_free(h,p);return NULL;}
    header *a=metadata(h,p);if(!a){h->failures++;return NULL;}
    size_t raw_size,charge;
    if(!sizes(h,a->alignment,bytes,a->charge,&raw_size,&charge)){h->failures++;return NULL;}
    header old=*a;size_t offset=(uintptr_t)p-(uintptr_t)old.raw;
    void *raw=h->resize(h->context,old.raw,raw_size);
    if(!raw){h->failures++;return NULL;}
    void *next=aligned(raw,old.alignment);
    /* realloc preserves the old offset, while the new raw address may have
     * different alignment. Move before writing the relocated header. */
    memmove(next,(char*)raw+offset,old.bytes<bytes?old.bytes:bytes);
    commit(h,next,raw,bytes,charge,old.alignment,old.charge);return next;
}
