#ifndef ACP_SHA256_H
#define ACP_SHA256_H
#include <stddef.h>
#include <stdint.h>

/* FIPS 180-4 SHA-256. No hardware service or allocation is needed. */
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} acp_sha256;

void acp_sha256_init(acp_sha256 *ctx);
void acp_sha256_update(acp_sha256 *ctx, const void *data, size_t size);
void acp_sha256_final(acp_sha256 *ctx, unsigned char digest[32]);
#endif
