/* Setup screen, drawn by the CPU into a 960x544 A8B8G8R8 frame before any
 * game or GPU code runs. Background: the official art already packaged as
 * app0:sce_sys/pic0.png, darkened under a bottom panel. Text: Josefin Sans
 * (SIL OFL 1.1), the same setup font as the other ports. */
#include "acp_setup_screen.h"
#include "acp_setup_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#define W 960
#define H 544
#define PANEL 280
#define LEFT 56
#define RIGHT 904
static uint32_t *art;
static uint32_t rgb(unsigned r,unsigned g,unsigned b){return 0xff000000u|b<<16|g<<8|r;}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int paeth(int a,int b,int c){int p=a+b-c,pa=abs(p-a),pb=abs(p-b),pc=abs(p-c);return pa<=pb&&pa<=pc?a:pb<=pc?b:c;}
/* 8-bit, non-interlaced PNG (palette, RGB or RGBA) into ABGR pixels. */
static int decode_png(const uint8_t *png,size_t size,uint32_t *out){
    static const uint8_t signature[8]={137,80,78,71,13,10,26,10};
    if(size<33||memcmp(png,signature,8))return -1;
    uint32_t width=0,height=0,palette[256]={0};int type=-1,channels=0;
    uint8_t *packed=malloc(size),*rows=NULL;size_t idat=0;int rc=-1;
    if(!packed)return -1;
    for(size_t o=8;o+12<=size;){
        uint32_t n=be32(png+o);const uint8_t *tag=png+o+4,*d=png+o+8;
        if(n>size-o-12)break;
        if(!memcmp(tag,"IHDR",4)&&n>=13){
            width=be32(d);height=be32(d+4);type=d[9];
            if(d[8]!=8||d[12]||width!=W||height!=H)goto done;
            channels=type==3?1:type==2?3:type==6?4:0;if(!channels)goto done;
        }else if(!memcmp(tag,"PLTE",4)){
            for(uint32_t i=0;i<n/3&&i<256;i++)palette[i]=rgb(d[3*i],d[3*i+1],d[3*i+2]);
        }else if(!memcmp(tag,"IDAT",4)){memcpy(packed+idat,d,n);idat+=n;}
        else if(!memcmp(tag,"IEND",4))break;
        o+=12+n;
    }
    if(!channels)goto done;
    size_t stride=(size_t)width*channels,need=(stride+1)*height;
    rows=malloc(need);if(!rows)goto done;
    uLongf length=(uLongf)need;
    if(uncompress(rows,&length,packed,(uLong)idat)!=Z_OK||length!=need)goto done;
    for(uint32_t y=0;y<height;y++){
        uint8_t *line=rows+y*(stride+1),filter=line[0],*px=line+1;
        const uint8_t *up=y?rows+(y-1)*(stride+1)+1:NULL;
        for(size_t x=0;x<stride;x++){
            int a=x>=(size_t)channels?px[x-channels]:0,b=up?up[x]:0,c=up&&x>=(size_t)channels?up[x-channels]:0;
            int add=filter==1?a:filter==2?b:filter==3?(a+b)/2:filter==4?paeth(a,b,c):0;
            if(filter>4)goto done;
            px[x]=(uint8_t)(px[x]+add);
        }
        for(uint32_t x=0;x<width;x++){
            const uint8_t *s=px+x*channels;
            out[y*W+x]=channels==1?palette[s[0]]:rgb(s[0],s[1],s[2]);
        }
    }
    rc=0;
done:
    free(packed);free(rows);return rc;
}
void acp_setup_screen_load(const char *path){
    art=malloc((size_t)W*H*4);if(!art)return;
    FILE *f=fopen(path,"rb");long size=-1;uint8_t *png=NULL;
    if(f&&!fseek(f,0,SEEK_END)&&(size=ftell(f))>0&&size<=1024*1024&&!fseek(f,0,SEEK_SET)&&(png=malloc((size_t)size))&&
       fread(png,1,(size_t)size,f)==(size_t)size&&!decode_png(png,(size_t)size,art)){
        /* Darken toward the bottom panel so the text stays readable. */
        for(unsigned y=0;y<H;y++){
            unsigned keep=y<PANEL-88?235:y<PANEL?235-(y-(PANEL-88))*190/88:45;
            for(unsigned x=0;x<W;x++){
                uint32_t v=art[y*W+x];
                art[y*W+x]=0xff000000u|((v>>16&255)*keep/255)<<16|((v>>8&255)*keep/255)<<8|((v&255)*keep/255);
            }
        }
    }else{free(art);art=NULL;}
    free(png);if(f)fclose(f);
}

static void fill(uint32_t *p,int x,int y,int w,int h,uint32_t color){
    for(int j=y;j<y+h&&j<H;j++)for(int i=x;i<x+w&&i<W;i++)if(i>=0&&j>=0)p[j*W+i]=color;
}
static uint32_t blend(uint32_t under,uint32_t color,unsigned a){
    uint32_t v=0xff000000u;
    for(unsigned s=0;s<24;s+=8)v|=((((color>>s)&255)*a+((under>>s)&255)*(255-a)+127)/255)<<s;
    return v;
}
static void glyph(uint32_t *p,int x,int y,unsigned char c,unsigned size,uint32_t color){
    if(c<32||c>126)c='?';
    const unsigned char *g=setup_font_pixels+(c-32)*28*32;
    for(unsigned j=0;j<32*size/24;j++)for(unsigned i=0;i<28*size/24;i++){
        unsigned a=g[(j*24/size)*28+i*24/size];
        int px=x+(int)i,py=y+(int)j;
        if(a&&px>=0&&px<W&&py>=0&&py<H)p[py*W+px]=blend(p[py*W+px],color,a);
    }
}
static unsigned advance(unsigned char c,unsigned size){return (setup_font_advance[c>=32&&c<=126?c-32:'?'-32]*size+12)/24;}
static int width(const char *s,unsigned size){int n=0;for(;*s;s++)n+=(int)advance((unsigned char)*s,size);return n;}
static int text(uint32_t *p,int x,int y,const char *s,unsigned size,uint32_t color){
    for(;*s;s++){if(p)glyph(p,x,y,(unsigned char)*s,size,color);x+=(int)advance((unsigned char)*s,size);}
    return x;
}
/* Word-wrapped between LEFT and RIGHT; returns the y below the last line. */
static int paragraph(uint32_t *p,int y,const char *s,unsigned size,uint32_t color){
    int x=LEFT,step=(int)(32*size/24)+2;
    while(*s){
        const char *word=s;while(*s&&*s!=' ')s++;
        char buffer[128];size_t n=(size_t)(s-word);if(n>=sizeof(buffer))n=sizeof(buffer)-1;
        memcpy(buffer,word,n);buffer[n]=0;
        int w=width(buffer,size);
        if(x>LEFT&&x+w>RIGHT){x=LEFT;y+=step;}
        x=text(p,x,y,buffer,size,color);
        while(*s==' '){x+=(int)advance(' ',size);s++;}
    }
    return y+step;
}
/* Button legends drawn like the system's: a ring for CIRCLE, a capsule for START. */
static void ring(uint32_t *p,int cx,int cy,int r,uint32_t color){
    for(int y=-r-1;y<=r+1;y++)for(int x=-r-1;x<=r+1;x++){
        int d2=x*x+y*y,outer=r*r,inner=(r-2)*(r-2);
        unsigned a=d2<=outer&&d2>=inner?255:0;
        if(!a&&d2<=(r+1)*(r+1)&&d2>outer)a=110;
        if(!a&&d2<inner&&d2>=(r-3)*(r-3))a=110;
        if(a&&cx+x>=0&&cx+x<W&&cy+y>=0&&cy+y<H)p[(cy+y)*W+cx+x]=blend(p[(cy+y)*W+cx+x],color,a);
    }
}
static int legend(uint32_t *p,int x,int y,const char *button,const char *action,uint32_t color){
    const unsigned size=22;int h=(int)(32*size/24);
    if(!strcmp(button,"CIRCLE")){ring(p,x+11,y+h/2-1,10,color);x+=30;}
    else{
        int w=width(button,14)+18;
        fill(p,x+6,y+5,w-12,h-10,color);fill(p,x+3,y+7,w-6,h-14,color);fill(p,x+4,y+6,w-8,h-12,color);
        text(p,x+9,y+8,button,14,0xff1a1a1a);x+=w+8;
    }
    return text(p,x,y,action,size,color)+40;
}
void acp_setup_screen(uint32_t *p,const char *message,unsigned percent,int mode){
    if(art)memcpy(p,art,(size_t)W*H*4);
    else fill(p,0,0,W,H,rgb(12,14,18));
    const uint32_t ink=rgb(240,236,226),muted=rgb(178,172,160),accent=rgb(222,178,96),alert=rgb(236,140,120);
    int y=PANEL+16;
    if(mode==1){
        text(p,LEFT,y,message&&*message?message:"Preparing",30,ink);
        char value[8];snprintf(value,sizeof(value),"%u%%",percent>100?100:percent);
        text(p,RIGHT-width(value,30),y,value,30,ink);
        y+=56;
        fill(p,LEFT,y,RIGHT-LEFT,6,rgb(70,66,60));
        fill(p,LEFT,y,(RIGHT-LEFT)*(int)(percent>100?100:percent)/100,6,accent);
        y+=24;
        paragraph(p,y,"This takes a few minutes. You can pause and continue later; finished parts are kept.",20,muted);
        legend(p,LEFT,H-44,"CIRCLE","Pause",ink);
    }else if(mode==2){
        text(p,LEFT,y,"Ready to play",30,ink);
        y=paragraph(p,y+52,"Keep game.apk and the OBB in ux0:data/acpirates/. The game needs them every time.",20,muted);
        if(message&&*message&&strcmp(message,"Ready to play."))paragraph(p,y,message,20,alert);
        legend(p,LEFT,H-44,"START","Play",ink);
    }else if(mode==3){
        text(p,LEFT,y,"Before you play",30,ink);
        paragraph(p,y+52,message&&*message?message:"Setup cannot continue.",22,alert);
        legend(p,LEFT,H-44,"CIRCLE","Close",ink);
    }else{
        text(p,LEFT,y,"Set up Assassin's Creed Pirates",30,ink);
        y+=48;
        y=paragraph(p,y,"Copy your Android game files into ux0:data/acpirates/ :",20,muted)+2;
        text(p,LEFT,y,"APK",20,accent);text(p,LEFT+60,y,"Assassin's Creed Pirates 2.9.1 (ARMv7), any file name",20,ink);y+=30;
        text(p,LEFT,y,"OBB",20,accent);text(p,LEFT+60,y,"main.253.com.ubisoft.assassin.pirates.obb",20,ink);y+=33;
        if(message&&*message)paragraph(p,y,message,20,alert);
        int x=legend(p,LEFT,H-44,"START","Set up",ink);
        legend(p,x,H-44,"CIRCLE","Close",ink);
    }
}
