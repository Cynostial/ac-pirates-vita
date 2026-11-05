#include "acp_fps.h"
#include <stdio.h>
#include <string.h>
static const unsigned char digits[10][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}};
static unsigned row(char c,unsigned y){
    static const unsigned char f[]={31,16,16,30,16,16,16};
    static const unsigned char p[]={30,17,17,30,16,16,16};
    static const unsigned char s[]={15,16,16,14,1,1,30};
    if(c>='0'&&c<='9')return digits[c-'0'][y];
    if(c=='F')return f[y];
    if(c=='P')return p[y];
    if(c=='S')return s[y];
    if(c=='.')return y==6?4:0;
    if(c=='-')return y==3?14:0;
    return 0;
}
void acp_fps_draw(acp_fps *f,uint32_t *pixels,unsigned width,unsigned height,unsigned stride,uint64_t now){
    if(!f||width<128||height<32||stride<width)return;
    if(!f->initialized){f->initialized=1;f->started=now;strcpy(f->label,"--.- FPS");}
    else{
        ++f->frames;
        if(now-f->started>=1000000){
            f->tenths=(unsigned)((uint64_t)f->frames*10000000/(now-f->started));
            if(f->tenths>9999)f->tenths=9999;
            snprintf(f->label,sizeof(f->label),"%u.%u FPS",f->tenths/10,f->tenths%10);
            f->started=now;f->frames=0;f->ready=1;
        }
    }
    if(!pixels)return;
    unsigned length=(unsigned)strlen(f->label),x0=8,y0=height-28;
    for(unsigned y=0;y<22;y++)for(unsigned x=0;x<length*12+8;x++)pixels[(y0+y)*stride+x0+x]=0xff101010;
    for(unsigned c=0;c<length;c++)for(unsigned y=0;y<7;y++){
        unsigned bits=row(f->label[c],y);
        for(unsigned x=0;x<5;x++)if(bits&(16u>>x))for(unsigned yy=0;yy<2;yy++)for(unsigned xx=0;xx<2;xx++)
            pixels[(y0+4+y*2+yy)*stride+x0+4+c*12+x*2+xx]=0xffffffff;
    }
}
