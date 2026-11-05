/* Every .spd asset (textures, meshes, scripts, data) is AES-128-CBC encrypted
 * and SparkUtils::DecryptBuffer copies each loaded buffer, then decrypts it
 * in software. A pre-decrypted archive (tools/build_plain_obb.py) stores the
 * decrypted bytes followed by a 16-byte marker: such a buffer only needs the
 * marker trimmed. Anything else (saves, the original archive) takes the
 * game's own copy-and-decode path, with the same arguments as the original. */
#include "acp_native.h"
#include "acp_game_patch.h"
#include <so_util/so_util.h>
#include <kubridge.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
static const unsigned char plain_marker[16]={'A','C','P','-','V','I','T','A','-','P','L','A','I','N','1',0};
static const unsigned char asset_key[16]={1,2,3,4,5,6,7,8,7,6,5,4,3,2,1,10};
static unsigned char *(*buffer_ptr)(void *);
static unsigned (*buffer_size)(const void *);
static void (*buffer_resize)(void *,unsigned);
static void (*buffer_create)(void *,unsigned);
static void (*buffer_destroy)(void *);
static void (*buffer_push)(void *,const void *,unsigned);
static unsigned (*aes_decode)(unsigned char *,unsigned,unsigned char *,unsigned,const unsigned char *,unsigned);
static unsigned plain_files,decoded_files;
static uint64_t plain_bytes,decoded_bytes,decoded_us;
static void report(int plain){
    unsigned n=plain?plain_files:decoded_files;
    if(n<=2||!(n%256))acp_log("Assets: %u pre-decrypted (%llu KiB); %u AES-decoded (%llu KiB, %llu ms)",
        plain_files,(unsigned long long)(plain_bytes/1024),decoded_files,
        (unsigned long long)(decoded_bytes/1024),(unsigned long long)(decoded_us/1000));
}
static int decrypt_buffer(void *buffer){
    unsigned char *p=buffer_ptr(buffer);unsigned n=buffer_size(buffer);
    if(p&&n>=sizeof(plain_marker)&&!memcmp(p+n-sizeof(plain_marker),plain_marker,sizeof(plain_marker))){
        buffer_resize(buffer,n-(unsigned)sizeof(plain_marker));
        __atomic_add_fetch(&plain_files,1,__ATOMIC_RELAXED);__atomic_add_fetch(&plain_bytes,n,__ATOMIC_RELAXED);
        report(1);return 1;
    }
    uint64_t begin=sceKernelGetProcessTimeWide();
    uint64_t copy[4]={0}; /* SparkUtils::MemoryBuffer, 12 bytes in the original frame */
    buffer_create(copy,n);buffer_push(copy,p,n);
    unsigned result=aes_decode(buffer_ptr(copy),buffer_size(copy),buffer_ptr(buffer),buffer_size(buffer),asset_key,128);
    if(result)buffer_resize(buffer,result);
    buffer_destroy(copy);
    __atomic_add_fetch(&decoded_files,1,__ATOMIC_RELAXED);__atomic_add_fetch(&decoded_bytes,n,__ATOMIC_RELAXED);
    __atomic_add_fetch(&decoded_us,sceKernelGetProcessTimeWide()-begin,__ATOMIC_RELAXED);
    report(0);return result!=0;
}
int acp_plain_assets_install(so_module *m){
    static const unsigned char entry_bytes[8]={0x2d,0xe9,0xf0,0x47,0x8a,0xb0,0x2b,0x4f};
    uintptr_t entry=so_symbol(m,"_ZN10SparkUtils13DecryptBufferERNS_12MemoryBufferE");
    buffer_ptr=(void *)so_symbol(m,"_ZN10SparkUtils12MemoryBuffer6GetPtrEv");
    buffer_size=(void *)so_symbol(m,"_ZNK10SparkUtils12MemoryBuffer7GetSizeEv");
    buffer_resize=(void *)so_symbol(m,"_ZN10SparkUtils12MemoryBuffer6ResizeEj");
    buffer_create=(void *)so_symbol(m,"_ZN10SparkUtils12MemoryBufferC1Ej");
    buffer_destroy=(void *)so_symbol(m,"_ZN10SparkUtils12MemoryBufferD1Ev");
    buffer_push=(void *)so_symbol(m,"_ZN10SparkUtils12MemoryBuffer8PushDataEPKvj");
    aes_decode=(void *)so_symbol(m,"_ZN10SparkUtils17DecodeToAESBufferEPhjS0_jS0_j");
    uint8_t jump[8];
    if(!entry||!buffer_ptr||!buffer_size||!buffer_resize||!buffer_create||!buffer_destroy||!buffer_push||!aes_decode||
       ((entry&~(uintptr_t)1)&3)||memcmp((void *)(entry&~(uintptr_t)1),entry_bytes,sizeof(entry_bytes))||
       acp_game_patch_build((uint32_t)(uintptr_t)decrypt_buffer,jump)){
        acp_log("Assets: DecryptBuffer not patched (unexpected library); original decoding kept");return 0;
    }
    if(kuKernelCpuUnrestrictedMemcpy((void *)(entry&~(uintptr_t)1),jump,sizeof(jump))<0)return -1;
    acp_log("Assets: pre-decrypted archive entries skip AES; others use the original decoder");
    return 0;
}
