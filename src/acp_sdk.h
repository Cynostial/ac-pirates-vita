#ifndef ACP_SDK_H
#define ACP_SDK_H
#include "acp_platform.h"
typedef struct {
    void *ctx;
    int (*random)(void *,unsigned char *,unsigned);
    int (*prepare_file)(void *,const char *,const char *);
    void (*log)(void *,const char *);
    const char *installation_id; /* persistent UUID, no account or device secret */
} acp_sdk_ops;
typedef struct {acp_platform *platform;acp_jni_registry *registry;acp_sdk_ops ops;char id[37];} acp_sdk;
int acp_sdk_configure(acp_sdk *,acp_platform *,acp_jni_registry *,const acp_sdk_ops *);
int acp_sdk_vita_configure(acp_platform *,acp_jni_registry *);
int acp_sdk_uuid(const unsigned char bytes[16],char text[37]);
#endif
