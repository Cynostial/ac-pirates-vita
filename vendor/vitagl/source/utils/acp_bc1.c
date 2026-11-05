/* AC Pirates adapter. ETC1 -> opaque BC1, one block at a time, directly into
 * the GXM swizzled allocation. No full-image RGB/RGBA shadow or scratch heap.
 * ETC decoder and stb_dxt are provided by the pinned VitaGL dependency. */
#include "acp_bc1.h"
#include "etc1_utils.h"
/* Pinned stb_dxt.h public ABI; its Vita header also imports GPU internals. */
extern void stb_compress_dxt_block(unsigned char *,const unsigned char *,int,int);
#define STB_DXT_HIGHQUAL 2
#include <string.h>
static unsigned blocks(unsigned n){unsigned b=1;while(b<(n+3)/4)b<<=1;return b;}
size_t acp_bc1_size(unsigned w,unsigned h){return (size_t)blocks(w)*blocks(h)*8;}
size_t acp_bc1_offset(unsigned x,unsigned y,unsigned w,unsigned h){
    unsigned bw=blocks(w),bh=blocks(h),index=0,bit=0;
    /* GXM places y in the low interleaved bit; rectangular textures append
     * the unmatched bits of the longer axis after the square tile. */
    while(bw>1||bh>1){
        if(bh>1){index|=(y&1)<<bit++;y>>=1;bh>>=1;}
        if(bw>1){index|=(x&1)<<bit++;x>>=1;bw>>=1;}
    }
    return (size_t)index*8;
}
void acp_etc1_to_bc1(const uint8_t *src,uint8_t *dst,unsigned w,unsigned h){
    unsigned nx=(w+3)/4,ny=(h+3)/4;
    memset(dst,0,acp_bc1_size(w,h));
    if(!src)return;
    for(unsigned y=0;y<ny;y++)for(unsigned x=0;x<nx;x++){
        uint8_t rgb[48],rgba[64];
        etc1_decode_block(src+((size_t)y*nx+x)*8,rgb);
        for(unsigned j=0;j<4;j++)for(unsigned i=0;i<4;i++){
            unsigned px=x*4+i<w?i:w-x*4-1;
            unsigned py=y*4+j<h?j:h-y*4-1;
            memcpy(rgba+(j*4+i)*4,rgb+(py*4+px)*3,3);
            rgba[(j*4+i)*4+3]=255;
        }
        stb_compress_dxt_block(dst+acp_bc1_offset(x,y,w,h),rgba,0,STB_DXT_HIGHQUAL);
    }
}
static void decode(const uint8_t *b,uint8_t rgba[64]){
    unsigned c0=b[0]|(b[1]<<8),c1=b[2]|(b[3]<<8);
    unsigned c[2]={c0,c1};uint8_t p[4][4];
    for(unsigned i=0;i<2;i++){
        unsigned r=(c[i]>>11)&31,g=(c[i]>>5)&63,bl=c[i]&31;
        p[i][0]=(r<<3)|(r>>2);p[i][1]=(g<<2)|(g>>4);p[i][2]=(bl<<3)|(bl>>2);p[i][3]=255;
    }
    for(unsigned k=0;k<3;k++){
        p[2][k]=c0>c1?(2*p[0][k]+p[1][k])/3:(p[0][k]+p[1][k])/2;
        p[3][k]=c0>c1?(p[0][k]+2*p[1][k])/3:0;
    }
    p[2][3]=p[3][3]=255; /* UBC1_1BGR, never transparent. */
    uint32_t indices=(uint32_t)b[4]|((uint32_t)b[5]<<8)|((uint32_t)b[6]<<16)|((uint32_t)b[7]<<24);
    for(unsigned i=0;i<16;i++){memcpy(rgba+4*i,p[indices&3],4);indices>>=2;}
}
/* Native GXM ETC1 (Vita hardware): VitaGL's swizzler stores each block's two
 * 32-bit words byte-swapped, in the same block order as BC1. */
static void decode_etc1(const uint8_t *b,uint8_t rgba[64]){
    uint8_t block[8],rgb[48];
    for(unsigned k=0;k<4;k++){block[k]=b[3-k];block[4+k]=b[7-k];}
    etc1_decode_block(block,rgb);
    for(unsigned i=0;i<16;i++){memcpy(rgba+4*i,rgb+3*i,3);rgba[4*i+3]=255;}
}
static int read_blocks(void (*decode_block)(const uint8_t *,uint8_t[64]),const uint8_t *src,unsigned tw,unsigned th,
                       int x,int y,int w,int h,uint8_t *dst,unsigned bpp,unsigned alignment){
    if(!src||!dst||x<0||y<0||w<0||h<0||(unsigned)x>tw||(unsigned)y>th||
       (unsigned)w>tw-(unsigned)x||(unsigned)h>th-(unsigned)y||
       (bpp!=3&&bpp!=4)||(alignment!=1&&alignment!=2&&alignment!=4&&alignment!=8))return -1;
    size_t stride=((size_t)w*bpp+alignment-1)&~(size_t)(alignment-1);
    for(unsigned by=(unsigned)y/4;by<((unsigned)y+h+3)/4;by++){
        for(unsigned bx=(unsigned)x/4;bx<((unsigned)x+w+3)/4;bx++){
            uint8_t rgba[64];decode_block(src+acp_bc1_offset(bx,by,tw,th),rgba);
            for(unsigned j=0;j<4;j++)for(unsigned i=0;i<4;i++){
                int dx=(int)(bx*4+i)-x,dy=(int)(by*4+j)-y;
                if(dx>=0&&dx<w&&dy>=0&&dy<h)memcpy(dst+(size_t)dy*stride+(size_t)dx*bpp,rgba+(j*4+i)*4,bpp);
            }
        }
    }
    return 0;
}
int acp_bc1_read(const uint8_t *src,unsigned tw,unsigned th,int x,int y,
                 int w,int h,uint8_t *dst,unsigned bpp,unsigned alignment){
    return read_blocks(decode,src,tw,th,x,y,w,h,dst,bpp,alignment);
}
int acp_etc1_read(const uint8_t *src,unsigned tw,unsigned th,int x,int y,
                  int w,int h,uint8_t *dst,unsigned bpp,unsigned alignment){
    return read_blocks(decode_etc1,src,tw,th,x,y,w,h,dst,bpp,alignment);
}
