/* AES-128-CBC decryption for the OBB's .spd entries (zero IV, the game's
 * SparkUtils key). T-table form of the equivalent inverse cipher; the tables
 * are generated once and checked against the FIPS-197 vector before use. */
#include "acp_setup.h"
#include <string.h>
static uint8_t sbox[256],inverse[256];
static uint32_t td[4][256],rk[44];
static const uint8_t KEY[16]={1,2,3,4,5,6,7,8,7,6,5,4,3,2,1,10};
static uint8_t mul(uint8_t a,uint8_t b){
    uint8_t r=0;
    while(b){if(b&1)r^=a;a=(uint8_t)((a<<1)^(a&0x80?0x1b:0));b>>=1;}
    return r;
}
static void tables(void){
    /* sbox[x] = affine(x^-1); walk the field with generator 3. */
    uint8_t p=1,q=1;
    do{
        p=(uint8_t)(p^(p<<1)^(p&0x80?0x1b:0));
        q^=q<<1;q^=q<<2;q^=q<<4;if(q&0x80)q^=0x09;
        uint8_t x=(uint8_t)(q^(q<<1|q>>7)^(q<<2|q>>6)^(q<<3|q>>5)^(q<<4|q>>4)^0x63);
        sbox[p]=x;
    }while(p!=1);
    sbox[0]=0x63;
    for(int i=0;i<256;i++)inverse[sbox[i]]=(uint8_t)i;
    for(int i=0;i<256;i++){
        uint8_t s=inverse[i];
        uint32_t w=(uint32_t)mul(s,14)<<24|(uint32_t)mul(s,9)<<16|(uint32_t)mul(s,13)<<8|mul(s,11);
        for(int t=0;t<4;t++){td[t][i]=w;w=w>>8|w<<24;}
    }
}
static uint32_t word(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void schedule(const uint8_t key[16]){
    uint32_t e[44];uint8_t rcon=1;
    for(int i=0;i<4;i++)e[i]=word(key+4*i);
    for(int i=4;i<44;i++){
        uint32_t t=e[i-1];
        if(!(i&3)){
            t=(uint32_t)sbox[(t>>16)&255]<<24^(uint32_t)sbox[(t>>8)&255]<<16^(uint32_t)sbox[t&255]<<8^sbox[t>>24]^(uint32_t)rcon<<24;
            rcon=mul(rcon,2);
        }
        e[i]=e[i-4]^t;
    }
    /* Reverse the round order; InvMixColumns on the inner round keys. */
    for(int r=0;r<=10;r++)for(int c=0;c<4;c++){
        uint32_t v=e[4*(10-r)+c];
        if(r&&r<10)v=td[0][sbox[v>>24]]^td[1][sbox[(v>>16)&255]]^td[2][sbox[(v>>8)&255]]^td[3][sbox[v&255]];
        rk[4*r+c]=v;
    }
}
static void block(const uint8_t in[16],uint8_t out[16]){
    const uint32_t *k=rk;
    uint32_t s0=word(in)^k[0],s1=word(in+4)^k[1],s2=word(in+8)^k[2],s3=word(in+12)^k[3],t0,t1,t2,t3;
    for(int r=1;r<10;r++){
        k+=4;
        t0=td[0][s0>>24]^td[1][(s3>>16)&255]^td[2][(s2>>8)&255]^td[3][s1&255]^k[0];
        t1=td[0][s1>>24]^td[1][(s0>>16)&255]^td[2][(s3>>8)&255]^td[3][s2&255]^k[1];
        t2=td[0][s2>>24]^td[1][(s1>>16)&255]^td[2][(s0>>8)&255]^td[3][s3&255]^k[2];
        t3=td[0][s3>>24]^td[1][(s2>>16)&255]^td[2][(s1>>8)&255]^td[3][s0&255]^k[3];
        s0=t0;s1=t1;s2=t2;s3=t3;
    }
    k+=4;
    put(out,   (uint32_t)inverse[s0>>24]<<24^(uint32_t)inverse[(s3>>16)&255]<<16^(uint32_t)inverse[(s2>>8)&255]<<8^inverse[s1&255]^k[0]);
    put(out+4, (uint32_t)inverse[s1>>24]<<24^(uint32_t)inverse[(s0>>16)&255]<<16^(uint32_t)inverse[(s3>>8)&255]<<8^inverse[s2&255]^k[1]);
    put(out+8, (uint32_t)inverse[s2>>24]<<24^(uint32_t)inverse[(s1>>16)&255]<<16^(uint32_t)inverse[(s0>>8)&255]<<8^inverse[s3&255]^k[2]);
    put(out+12,(uint32_t)inverse[s3>>24]<<24^(uint32_t)inverse[(s2>>16)&255]<<16^(uint32_t)inverse[(s1>>8)&255]<<8^inverse[s0&255]^k[3]);
}
int acp_setup_aes_selftest(void){
    static const uint8_t key[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    static const uint8_t cipher[16]={0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    static const uint8_t plain[16]={0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    uint8_t out[16];tables();schedule(key);block(cipher,out);
    int ok=!memcmp(out,plain,16);
    schedule(KEY);
    return ok?0:-1;
}
void acp_setup_aes_init(void){tables();schedule(KEY);}
void acp_setup_aes_cbc_decrypt(uint8_t *data,uint32_t bytes){
    uint8_t chain[16]={0},next[16],out[16];
    for(uint32_t i=0;i+16<=bytes;i+=16){
        memcpy(next,data+i,16);block(next,out);
        for(int j=0;j<16;j++)data[i+j]=out[j]^chain[j];
        memcpy(chain,next,16);
    }
}
