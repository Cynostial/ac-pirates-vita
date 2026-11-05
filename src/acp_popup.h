#ifndef ACP_POPUP_H
#define ACP_POPUP_H
#include "acp_jni_runtime.h"
#define ACP_POPUP_SLOTS 8
#define ACP_POPUP_CLASS "org/ubisoft/geea/spark2/PopupJava"
#define ACP_POPUP_SHOW_SIG "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZZZZZ)V"
typedef struct {jint id;char text[4096],buttons[3][128];} acp_popup_request;
typedef struct {
    void *ctx;
    void (*lock)(void *),(*unlock)(void *);
    /* start: 0 opened, 1 temporarily busy, -1 failed. poll: 0 waiting,
     * 1 cancel, 2 first alternative, 3 second alternative, -1 failure.
     * Both run on the rendering thread under the service mutex. */
    int (*start)(void *,const acp_popup_request *);
    int (*poll)(void *);
    void (*log)(void *,const char *);
} acp_popup_ops;
struct acp_popup;
typedef struct {struct acp_popup *owner;unsigned operation;} acp_popup_binding;
typedef struct acp_popup {
    acp_popup_ops ops;
    struct {acp_popup_request request;unsigned state,response;uint64_t order;} slots[ACP_POPUP_SLOTS];
    unsigned active,notifications_logged;
    uint64_t next_order;
    acp_popup_binding bindings[9];
} acp_popup;
int acp_popup_configure(acp_popup *,acp_jni_runtime *,const acp_popup_ops *);
int acp_popup_frame(acp_popup *);
int acp_popup_vita_configure(acp_jni_runtime *);
int acp_popup_vita_frame(void);
#endif
