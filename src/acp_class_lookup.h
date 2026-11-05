#ifndef ACP_CLASS_LOOKUP_H
#define ACP_CLASS_LOOKUP_H
typedef int (*acp_class_index_fn)(void *);
typedef void (*acp_class_call_fn)(void *,int,int);
void acp_class_lookup_original(acp_class_index_fn fn,acp_class_call_fn call);
int acp_class_index(void *state);
void acp_class_lookup_counts(unsigned out[3]);
#endif
