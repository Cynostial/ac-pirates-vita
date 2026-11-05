#include "acp_native.h"
#include "memory_budget.h"
#include "acp_graphics.h"
#include "acp_assets.h"
#include "acp_game_patch.h"
#include "acp_lua_vita.h"
#include "acp_sha256.h"
#include <so_util/so_util.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <kubridge.h>
#include <string.h>
static so_module module;
static acp_lua_vita lua;
static _Thread_local const char *last_error;
void acp_game_memory_report(void){
    size_t used,peak;acp_memory_usage(&used,&peak);
    acp_log("Game heap: used=%u KiB peak=%u KiB",(unsigned)(used/1024),(unsigned)(peak/1024));
    lua.heap.ops.lock(lua.heap.ops.ctx);
    acp_log("Lua heap: used=%u KiB peak=%u KiB blocks=%u failures=%u limit=%u KiB (extended %u KiB) last-failed-request=%u bytes",
        (unsigned)(lua.heap.used/1024),(unsigned)(lua.heap.peak/1024),(unsigned)lua.heap.blocks,
        lua.heap.failures,(unsigned)(lua.heap.limit/1024),(unsigned)(lua.heap.grown/1024),(unsigned)lua.heap.last_failure);
    lua.heap.ops.unlock(lua.heap.ops.ctx);
    SceKernelFreeMemorySizeInfo info={.size=sizeof(info)};
    if(sceKernelGetFreeMemorySize(&info)>=0)acp_log("Free memory: main %d KiB, CDRAM %d KiB",info.size_user/1024,info.size_cdram/1024);
}
extern unsigned char __exidx_start[],__exidx_end[];
uintptr_t acp_resolve(const char *name){
    uintptr_t p=acp_graphics_resolve(name);if(p)return p;
    p=acp_assets_resolve(name);if(p)return p;
    p=acp_ndk_resolve(name);if(p)return p;
    p=acp_extra_resolve(name);if(p)return p;
#define SEARCH(group) for(size_t i=0;i<acp_##group##_symbol_count;i++)if(!strcmp(name,acp_##group##_symbols[i].name))return acp_##group##_symbols[i].address
    SEARCH(file);SEARCH(thread);SEARCH(system);SEARCH(libc);
    return 0;
}
void *acp_dlopen(const char *path,int flags){
    (void)flags;if(!path)return &module;
    const char *name=strrchr(path,'/');name=name?name+1:path;
    const char *known[]={"libSparkApp.so","libc.so","libm.so","libdl.so","libGLESv2.so","libEGL.so","libOpenSLES.so","libandroid.so","liblog.so","libz.so"};
    for(size_t i=0;i<sizeof(known)/sizeof(*known);i++)if(!strcmp(name,known[i]))return &module;
    last_error="Library is unavailable on Vita";return NULL;
}
void *acp_dlsym(void *handle,const char *name){
    (void)handle;uintptr_t p=acp_resolve(name);if(!p)p=so_symbol(&module,name);
    if(!p){last_error="Symbol is unavailable on Vita";acp_log("dlsym unresolved: %s",name);}
    return (void*)p;
}
int acp_dlclose(void *handle){return handle==&module?0:acp_error(22);}
const char *acp_dlerror(void){const char *s=last_error;last_error=NULL;return s;}
uintptr_t acp_find_exidx(uintptr_t pc,int *count){
    if(pc>=module.text_base&&pc-module.text_base<module.text_size){*count=module.exidx_size/8;return module.exidx_base;}
    *count=(__exidx_end-__exidx_start)/8;return (uintptr_t)__exidx_start;
}
int acp_code_check(void *ctx,const void *function){
    (void)ctx;uintptr_t p=(uintptr_t)function&~1u;
    return p>=module.text_base&&p-module.text_base<module.text_size;
}
static int mapped_word(uintptr_t p){
    if(p>=module.text_base&&p-module.text_base<=module.text_size-4)return 1;
    for(int i=0;i<module.n_data;i++)if(p>=module.data_base[i]&&p-module.data_base[i]<=module.data_size[i]-4)return 1;
    return 0;
}
static void relocate(void){
    unsigned imports=0;
    /* Reject missing strong imports before calling even one ELF initializer. */
    for(unsigned i=0;i<module.num_dynsym;i++){
        const Elf32_Sym *s=module.dynsym+i;
        if(!s->st_name||s->st_shndx!=SHN_UNDEF||ELF32_ST_BIND(s->st_info)==STB_WEAK)continue;
        const char *name=module.dynstr+s->st_name;
        if(!acp_resolve(name))acp_fatal("Unresolved required import: %s",name);
        imports++;
    }
    for(unsigned i=0;i<module.num_reldyn+module.num_relplt;i++){
        const Elf32_Rel *r=i<module.num_reldyn?module.reldyn+i:module.relplt+i-module.num_reldyn;
        unsigned type=ELF32_R_TYPE(r->r_info),si=ELF32_R_SYM(r->r_info);
        uintptr_t address=module.load_addr+r->r_offset;
        if(si>=module.num_dynsym||!mapped_word(address))acp_fatal("Invalid relocation");
        Elf32_Sym *s=module.dynsym+si;uint32_t value=0,old=*(uint32_t*)address;
        if(type==R_ARM_NONE)continue;
        if(type==R_ARM_RELATIVE)value=old+module.load_addr;
        else {
            uintptr_t target=s->st_shndx==SHN_UNDEF?acp_resolve(module.dynstr+s->st_name):module.load_addr+s->st_value;
            if(type==R_ARM_ABS32)value=old+target;
            else if(type==R_ARM_GLOB_DAT||type==R_ARM_JUMP_SLOT)value=target;
            else acp_fatal("Unsupported relocation type %u",type);
        }
        if(kuKernelCpuUnrestrictedMemcpy((void*)address,&value,4)<0)acp_fatal("Relocation write failed");
    }
    acp_log("Resolved %u strong imports; applied %u relocations",imports,module.num_reldyn+module.num_relplt);
}
static void no_android_crash_reporter(void *vm){(void)vm;}
/* A full SHA-256 of the library costs ~2.5 s at 333 MHz on every launch. After
 * a full verification, record the file's size and modification time with the
 * expected digest; an unchanged file skips only the hash, never the read. */
#define LIBRARY_PATH ACP_DATA_PATH "/libSparkApp.so"
#define LIBRARY_RECORD ACP_DATA_PATH "/libSparkApp.verified"
typedef struct {char magic[8];unsigned size;SceDateTime mtime;unsigned char digest[32];} LibraryRecord;
static int library_stat(unsigned size,const unsigned char *expected,LibraryRecord *out){
    SceIoStat st;if(sceIoGetstat(LIBRARY_PATH,&st)<0||st.st_size!=(SceOff)size)return -1;
    memset(out,0,sizeof(*out));memcpy(out->magic,"ACPLIB1",8);out->size=size;out->mtime=st.st_mtime;memcpy(out->digest,expected,32);
    return 0;
}
static int library_recorded(const LibraryRecord *current){
    LibraryRecord saved;int fd=sceIoOpen(LIBRARY_RECORD,SCE_O_RDONLY,0);if(fd<0)return 0;
    int n=sceIoRead(fd,&saved,sizeof(saved));sceIoClose(fd);
    return n==(int)sizeof(saved)&&!memcmp(&saved,current,sizeof(saved));
}
static void library_record(const LibraryRecord *current){
    int fd=sceIoOpen(LIBRARY_RECORD,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd<0)return;
    int n=sceIoWrite(fd,current,sizeof(*current));sceIoClose(fd);
    if(n!=(int)sizeof(*current))sceIoRemove(LIBRARY_RECORD);
}
int acp_load_game(void){
    static const unsigned char expected[32]={0x1d,0xc7,0xf2,0x64,0xf7,0x59,0x11,0x93,0xdd,0xf8,0xa2,0x34,0x25,0x0f,0x39,0x19,0x51,0x1f,0x3d,0xda,0x16,0xb5,0xc3,0x83,0xde,0x9b,0x66,0xc1,0x7c,0xc2,0x91,0xa7};
    const unsigned size=18030972;
    LibraryRecord current;int have_stat=!library_stat(size,expected,&current);
    int trusted=have_stat&&library_recorded(&current);
    int fd=sceIoOpen(LIBRARY_PATH,SCE_O_RDONLY,0);
    if(fd<0||sceIoLseek(fd,0,SCE_SEEK_END)!=size)acp_fatal("Missing or wrong libSparkApp.so");
    sceIoLseek(fd,0,SCE_SEEK_SET);
    SceUID block=sceKernelAllocMemBlock("AC verified input",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,(size+4095)&~4095u,NULL);
    void *data=NULL;
    if(block<0||sceKernelGetMemBlockBase(block,&data)<0)acp_fatal("Cannot allocate library input");
    acp_sha256 hash;acp_sha256_init(&hash);
    unsigned done=0;
    while(done<size){
        int n=sceIoRead(fd,(char*)data+done,size-done>262144?262144:size-done);
        if(n<=0)acp_fatal("Incomplete library read");
        if(!trusted)acp_sha256_update(&hash,(char*)data+done,(unsigned)n);
        done+=n;
    }
    sceIoClose(fd);
    if(trusted)acp_log("Library unchanged since full SHA256 verification; mapping ARMv7 ELF");
    else{
        unsigned char digest[32];acp_sha256_final(&hash,digest);
        if(memcmp(expected,digest,32))acp_fatal("Library SHA256 mismatch");
        if(have_stat)library_record(&current);
        acp_log("Library SHA256 verified; mapping ARMv7 ELF");
    }
    if(so_mem_load(&module,data,size,0x98000000)<0)acp_fatal("ELF mapping failed; kubridge required");
    sceKernelFreeMemBlock(block);
    relocate();
    acp_log("Allocating game and Lua heaps");
    if(acp_memory_init()||acp_lua_vita_create(&lua,ACP_LUA_HEAP_BYTES)||acp_game_patch_vita(&module,&lua.heap))
        acp_fatal("Heap allocation or verified game patch failed");
    /* This function installs Linux signal/corkscrew reporting only. Vita has
     * no Android crash-report service; leave game logic and error paths intact. */
    unsigned char jump[8];
    if(acp_game_patch_build((uintptr_t)no_android_crash_reporter,jump)||
       kuKernelCpuUnrestrictedMemcpy((void*)(module.load_addr+0x52dadc),jump,8)<0)
        acp_fatal("Crash backend adaptation failed");
    extern int acp_engine_profile_install(so_module *);
    extern int acp_gc_window_install(so_module *);
    if(acp_gc_window_install(&module))acp_fatal("Collector window guard failed");
    extern int acp_animation_nodes_install(so_module *);
    if(acp_animation_nodes_install(&module))acp_fatal("Animation-node guard failed");
    extern int acp_live_uniforms_install(so_module *);
    if(acp_live_uniforms_install(&module))acp_fatal("Live material uniform guard failed");
#ifndef ACP_LUAJIT21
    extern int acp_class_lookup_install(so_module *);
    if(acp_class_lookup_install(&module))acp_fatal("Class-property guard failed");
#ifdef ACP_DEVELOPMENT_PROFILE
    extern int acp_callback_profile_install(so_module *);
    if(acp_callback_profile_install(&module))acp_fatal("Callback sampler guard failed");
#endif
#endif
    if(acp_engine_profile_install(&module))acp_fatal("Engine phase timing guard failed");
    extern int acp_display_bridge_install(so_module *);
    if(acp_display_bridge_install(&module))acp_fatal("Display rotation binding guard failed");
    extern int acp_matrix_install(so_module *);
    if(acp_matrix_install(&module))acp_fatal("Renderer matrix guard failed");
    extern int acp_mesh_transform_install(so_module *);
    if(acp_mesh_transform_install(&module))acp_fatal("Mesh transform guard failed");
    extern int acp_visibility_cache_install(so_module *);
    if(acp_visibility_cache_install(&module))acp_fatal("Visibility reuse guard failed");
    extern int acp_render_scopes_install(so_module *);
    if(acp_render_scopes_install(&module))acp_fatal("Render scope guard failed");
    extern int acp_script_errors_install(so_module *);
    if(acp_script_errors_install(&module))acp_fatal("Script error reporting guard failed");
    extern int acp_texture_flip_install(so_module *);
    if(acp_texture_flip_install(&module))acp_fatal("Texture flip guard failed");
    extern int acp_destroyed_checks_install(so_module *);
    if(acp_destroyed_checks_install(&module))acp_fatal("Collision report guard failed");
    extern int acp_contact_reports_install(so_module *);
    if(acp_contact_reports_install(&module))acp_fatal("Contact report guard failed");
    extern int acp_animation_stats_install(so_module *);
    if(acp_animation_stats_install(&module))acp_fatal("Animation statistics guard failed");
    so_flush_caches(&module);acp_log("ELF ready; game and Lua heaps allocated");return 0;
}
void acp_game_initialize(void){acp_log("Running %u native initializers",module.num_init_array);so_initialize(&module);}
