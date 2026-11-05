#ifndef ACP_MANIFEST_H
#define ACP_MANIFEST_H
#include "acp_jni_runtime.h"

/* One immutable index, shared by all JarFile/map/vector views. Names remain
 * outside the small JNI payload pool; subList/toArray materialize a batch. */
typedef struct {
    char *names;
    const char **entry;
    unsigned count,bytes;
    void (*status)(const char *); /* Optional, installed before JNI threads attach. */
} acp_manifest;
int acp_manifest_configure(acp_manifest *,acp_jni_runtime *,const char *index_path);
/* Call only after all JNI objects borrowing this index have been released. */
void acp_manifest_dispose(acp_manifest *);
#endif
