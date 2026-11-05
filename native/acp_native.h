#ifndef ACP_NATIVE_H
#define ACP_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ACP_DATA_PATH "ux0:data/acpirates"
#define ACP_VIRTUAL_PATH "/data/data/com.ubisoft.assassin.pirates"
typedef struct {const char *name; uintptr_t address;} AcpSymbol;
typedef struct {int32_t sec,nsec;} AcpTimespec;
typedef struct {int32_t sec,usec;} AcpTimeval;
void acp_log(const char *,...) __attribute__((format(printf,1,2)));
void acp_log_init(void);
void acp_log_flush(void);
void acp_save_backup(void);
#define ACP_LOADING_LAUNCH 1u
#define ACP_LOADING_SCREEN 2u
void acp_loading_boost(unsigned reason,int on);
void acp_loading_boost_check(void);
void acp_fatal(const char *,...) __attribute__((noreturn,format(printf,1,2)));
int acp_errno_code(int);
int *acp_errno_location(void);
int acp_error(int);
int acp_clock_gettime(int,AcpTimespec *);
const char *acp_path(const char *,char[1024]);
void *acp_dlopen(const char *,int);
void *acp_dlsym(void *,const char *);
int acp_dlclose(void *);
const char *acp_dlerror(void);
uintptr_t acp_find_exidx(uintptr_t,int *);
uintptr_t acp_resolve(const char *);
uintptr_t acp_ndk_resolve(const char *);
uintptr_t acp_extra_resolve(const char *);
void acp_libc_init(void);
void acp_stdio_init(void);
int acp_memory_init(void);
void *acp_malloc(size_t);
void *acp_calloc(size_t,size_t);
void *acp_realloc(void *,size_t);
void *acp_memalign(size_t,size_t);
void acp_free(void *);
size_t acp_usable(void *);
/* The range lies in game-heap memory (arena, spill pool or Lua loan). */
int acp_memory_writable(const void *,size_t);
/* The trophy setup dialog is open (trophies.c). */
int acp_trophy_dialog_frame(void);
char *acp_strdup(const char *);
void acp_memory_usage(size_t *,size_t *);
#define ACP_SYMBOLS(group) extern const AcpSymbol acp_##group##_symbols[]; extern const size_t acp_##group##_symbol_count
ACP_SYMBOLS(libc);
ACP_SYMBOLS(file);
ACP_SYMBOLS(system);
ACP_SYMBOLS(thread);
int acp_start_activity(void *,void *,void *,void *);
int acp_activity_finished(void);
int acp_window_prepare(void);
#ifdef __cplusplus
}
#endif
#endif
