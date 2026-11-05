#ifndef ACP_EGL_H
#define ACP_EGL_H
#include <stdint.h>
/* Single GLES2 context. Lifecycle calls are confined to the engine render
 * thread. Backend storage remains reserved for process lifetime. Surface
 * destruction/recreation does not destroy GPU resources. */
typedef struct {
    uintptr_t (*thread_id)(void);
    int (*initialize)(void);
    void (*swap)(void);
    void (*interval)(int);
    int (*window_valid)(void *);
    uintptr_t (*resolve_gl)(const char *);
} acp_egl_backend;
int acp_egl_install(const acp_egl_backend *);
uintptr_t acp_egl_resolve(const char *);
int acp_egl_current(void);
#endif
