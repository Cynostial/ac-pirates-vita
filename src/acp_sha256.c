#include "acp_sha256.h"
#include <string.h>

static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t ror(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
static uint32_t read_be(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void transform(acp_sha256 *ctx, const unsigned char *block) {
    uint32_t w[64];
    for (unsigned i=0; i<16; i++) w[i]=read_be(block+4*i);
    for (unsigned i=16; i<64; i++) {
        uint32_t s0=ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    uint32_t a=ctx->state[0], b=ctx->state[1], c=ctx->state[2], d=ctx->state[3];
    uint32_t e=ctx->state[4], f=ctx->state[5], g=ctx->state[6], h=ctx->state[7];
    for (unsigned i=0; i<64; i++) {
        uint32_t s1=ror(e,6)^ror(e,11)^ror(e,25), ch=(e&f)^((~e)&g);
        uint32_t t1=h+s1+ch+constants[i]+w[i];
        uint32_t s0=ror(a,2)^ror(a,13)^ror(a,22), maj=(a&b)^(a&c)^(b&c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+s0+maj;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}
void acp_sha256_init(acp_sha256 *ctx) {
    static const uint32_t initial[8]={
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    memset(ctx,0,sizeof(*ctx));
    memcpy(ctx->state,initial,sizeof(initial));
}
void acp_sha256_update(acp_sha256 *ctx, const void *data, size_t size) {
    const unsigned char *p=data;
    if (!size) return;
    ctx->bytes+=size;
    if (ctx->used) {
        size_t n=64-ctx->used;
        if (n>size) n=size;
        memcpy(ctx->block+ctx->used,p,n);
        ctx->used+=n; p+=n; size-=n;
        if (ctx->used==64) { transform(ctx,ctx->block); ctx->used=0; }
    }
    while (size>=64) { transform(ctx,p); p+=64; size-=64; }
    if (size) { memcpy(ctx->block,p,size); ctx->used=size; }
}
void acp_sha256_final(acp_sha256 *ctx, unsigned char digest[32]) {
    uint64_t bits=ctx->bytes*8;
    ctx->block[ctx->used++]=0x80;
    if (ctx->used>56) {
        memset(ctx->block+ctx->used,0,64-ctx->used);
        transform(ctx,ctx->block); ctx->used=0;
    }
    memset(ctx->block+ctx->used,0,56-ctx->used);
    for (unsigned i=0; i<8; i++) ctx->block[63-i]=(unsigned char)(bits>>(i*8));
    transform(ctx,ctx->block);
    for (unsigned i=0; i<32; i++) digest[i]=(unsigned char)(ctx->state[i/4]>>(24-8*(i%4)));
    memset(ctx,0,sizeof(*ctx));
}
