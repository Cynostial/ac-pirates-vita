#pragma once
/* First-launch setup: builds ux0:data/acpirates from the player's own APK and
 * OBB. The core (acp_setup.c) is portable C with no UI, thread or graphics
 * calls so it also runs on the PC (tests/test_setup_host.py). */
#include <stdint.h>
#define ACP_SETUP_ROOT "ux0:data/acpirates"
#define ACP_SETUP_APK "game.apk"
#define ACP_SETUP_OBB "main.253.com.ubisoft.assassin.pirates.obb"
#define ACP_SETUP_ORDER "app0:setup/obb-order.bin"
/* A game file unpacked from the APK: output path, APK member, content. */
typedef struct {
    const char *path,*member;
    uint32_t bytes,crc;
    uint8_t hash[32];
} acp_setup_file;
typedef struct { const char *key;uint32_t index; } acp_setup_string;
/* installer/acp_setup_table.c (tools/prepare_setup.py): metadata only. */
extern const uint32_t acp_setup_apk_size,acp_setup_plain_size,acp_setup_obb_entries,acp_setup_order_size;
extern const uint32_t acp_setup_cache_size,acp_setup_arsc_size,acp_setup_args_size;
extern const uint8_t acp_setup_apk_directory_hash[32],acp_setup_plain_hash[32],acp_setup_cache_hash[32];
extern const uint8_t acp_setup_obb_names_hash[32],acp_setup_order_hash[32],acp_setup_arsc_hash[32];
extern const uint8_t acp_setup_args_hash[32],acp_setup_identity[32];
/* setup/obb-order.bin in the VPK; the PC tests point it at installer/. */
extern const char *acp_setup_order_path;
extern const acp_setup_file acp_setup_files[];
extern const unsigned acp_setup_file_count;
extern const acp_setup_string acp_setup_strings[];
extern const unsigned acp_setup_string_count;
/* Byte-based progress. A nonzero return pauses setup (the player's request). */
typedef int (*acp_setup_progress)(const char *phase,uint64_t done,uint64_t total,void *arg);
typedef struct {
    char message[256],apk[512],obb[512];
    int plain_ready;            /* the converted OBB is already in place */
    unsigned completed,reused;
    unsigned loose;             /* assets unpacked because the APK's layout is not the pinned one */
} acp_setup_result;
/* 0 when the inputs setup needs are present (paths in apk/obb), else -1 with
 * a message for the player. */
int acp_setup_find(const char *root,acp_setup_result *);
/* Free space needed for acp_setup_run with the inputs acp_setup_find chose. */
uint64_t acp_setup_space(const char *root,const acp_setup_result *);
/* 0 ready, -2 paused, -1 failed (message says why). */
int acp_setup_run(const char *root,acp_setup_result *,acp_setup_progress,void *);
/* Marker written by a complete run, and every output at its expected size. */
int acp_setup_ready(const char *root);
/* AES-128-CBC decryption in place with the OBB key; 0 on success. */
int acp_setup_aes_selftest(void);
void acp_setup_aes_init(void);
void acp_setup_aes_cbc_decrypt(uint8_t *data,uint32_t bytes);
/* Vita front end (acp_setup_vita.c): returns when the game may start. */
void acp_setup_on_vita(void);
