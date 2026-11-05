/* Read-only shortcut for the pinned LuaJIT 2.0.2 ClassSystem __index closure.
 * String properties use the original hash layout; function getters use the
 * original lua_call with exactly its original stack and result convention.
 * Unusual arguments, callable objects and other keys retain the old callback.
 * No cached values, allocation, table writes or GC barrier changes. */
#include "acp_class_lookup.h"
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t value,tag;} Value;
typedef struct Node {Value value,key;struct Node *next;void *free_top;} Node;
typedef struct {uint8_t header[20];Node *node;uint32_t array_size,mask;} Table;
typedef struct {uint8_t header[24];Value upvalue[2];} Closure;
typedef struct {uint8_t header[8];uint8_t *global;uint32_t gc;Value *base,*top,*maxstack;} State;
_Static_assert(sizeof(void *)==4 && sizeof(Value)==8 && sizeof(Node)==24,"Pinned ARM32 VM layout");
_Static_assert(offsetof(Table,node)==20 && offsetof(Table,mask)==28,"Pinned table layout");
_Static_assert(offsetof(State,base)==16 && offsetof(State,top)==20,"Pinned Lua state layout");
_Static_assert(offsetof(Closure,upvalue)==24,"Pinned C closure layout");
static acp_class_index_fn original;
static acp_class_call_fn invoke;
#ifdef ACP_CLASS_COUNTERS
extern volatile unsigned acp_presented_frames;
static unsigned counts[3];
#define COUNT(n) do{if(!(acp_presented_frames&255u))__atomic_fetch_add(counts+(n),1,__ATOMIC_RELAXED);}while(0)
void acp_class_lookup_counts(unsigned out[3]){
    for(unsigned i=0;i<3;i++)out[i]=__atomic_exchange_n(counts+i,0,__ATOMIC_RELAXED);
}
#else
#define COUNT(n) ((void)0)
#endif
void acp_class_lookup_original(acp_class_index_fn fn,acp_class_call_fn call){original=fn;invoke=call;}
static const Value *find_string(const Table *table,uint32_t key,uint32_t hash){
    const Node *node=table->node+(hash&table->mask);
    do {
        if(node->key.tag==~4u && node->key.value==key)return &node->value;
        node=node->next;
    }while(node);
    return NULL;
}
int acp_class_index(void *opaque){
    State *state=opaque;Value *base=state->base;
    if(state->top!=base+2 || base[1].tag!=~4u){COUNT(2);return original(opaque);}
    const Closure *closure=(const void *)(uintptr_t)base[-1].value;
    if(closure->header[6]!=1 || closure->header[7]!=2 ||
       closure->upvalue[0].tag!=~11u || closure->upvalue[1].tag!=~11u){COUNT(2);return original(opaque);}
    uint32_t key=base[1].value,hash=*(const uint32_t *)((uintptr_t)key+8);
    const Value *getter=find_string((const void *)(uintptr_t)closure->upvalue[1].value,key,hash);
    if(getter && getter->tag!=~0u){
        if(getter->tag==~8u && invoke && state->top+2<state->maxstack){
            Value *top=state->top;
            top[0]=*getter;top[1]=base[0];state->top=top+2;
            COUNT(1);invoke(opaque,1,-1);
            /* lua_call may relocate the stack; reload both live pointers. */
            return (int)(state->top-state->base)-2;
        }
        COUNT(2);return original(opaque);
    }
    const Value *value=find_string((const void *)(uintptr_t)closure->upvalue[0].value,key,hash);
    /* The original no-getter path replaces its key slot with the result. */
    base[1]=value?*value:*(const Value *)(state->global+88);
    COUNT(0);
    return 1;
}
