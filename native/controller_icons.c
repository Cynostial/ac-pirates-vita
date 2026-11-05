/* Register tiny controller textures with the original engine. They are drawn
 * by the game's GUI materials and font renderer, with no extra GL overlay. */
#include "acp_native.h"
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

static void *(*construct)(void*,const char*,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,bool,bool,unsigned,bool,bool);
static void (*set_data)(void*,const unsigned char*,const void*,unsigned,unsigned);
static void *(*register_texture)(void*);
static void (*pin)(void*,void*,bool);
static int exports(void){
    if(construct&&set_data&&register_texture&&pin)return 1;
    construct=acp_dlsym(NULL,"_ZN9geTextureC1EPKcjjjjj15geTextureFormatbbjbb");
    set_data=acp_dlsym(NULL,"_ZN9geTexture7SetDataEPKhPK5geBoxjj");
    register_texture=acp_dlsym(NULL,"_ZN13LuaGeeaEngine15RegisterTextureEP9geTexture");
    pin=acp_dlsym(NULL,"_ZN14SparkResources8Resource12AddReferenceEPNS_16ResourceObserverEb");
    return construct&&set_data&&register_texture&&pin;
}
static int upload(const char *name,const unsigned short *pixels){
    void *texture=acp_calloc(1,12);if(!texture)return 0;
    construct(texture,name,64,64,1,1,1,0x13,false,false,1,false,false);
    set_data(texture,(const unsigned char*)pixels,NULL,0,0);
    void *resource=register_texture(texture);if(!resource)return 0;
    pin(resource,NULL,true);return 1;
}

int acp_hold_textures_load(void){
    static int ready;
    if(ready)return ready>0;
    if(!exports()){ready=-1;return 0;}
    /* 32 antialiased frames, 256 KiB total, loaded only for the stick lesson.
     * One foreground GUI quad + one dim backing ring; no new shader, per-frame
     * mesh allocation, native overlay, or dependency on online-profile assets. */
    unsigned char alpha[4096],segment[4096];unsigned short pixels[4096];
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
        unsigned i=y*64+x;float dx=(float)x-31.5f,dy=(float)y-31.5f;
        float radius=sqrtf(dx*dx+dy*dy),coverage=fminf(radius-26.0f,30.0f-radius);
        alpha[i]=(unsigned char)(15.0f*fmaxf(0,fminf(1,coverage))+0.5f);
        float turn=atan2f(dx,-dy)/6.28318530718f;if(turn<0)turn+=1;
        segment[i]=(unsigned char)fminf(31,floorf(turn*32));
    }
    for(unsigned frame=1;frame<=32;frame++){
        for(unsigned i=0;i<4096;i++)pixels[i]=(unsigned short)(0xfff0u|(segment[i]<frame?alpha[i]:0));
        char name[32];snprintf(name,sizeof(name),"ACPVitaHold%02u",frame);
        if(!upload(name,pixels)){ready=-1;return 0;}
    }
    ready=1;acp_log("Tutorial progress textures registered: 32 frames, 256 KiB");return 1;
}

int acp_controller_icons_load(void){
    static int ready;
    if(ready)return ready>0;
    if(!exports()){ready=-1;acp_log("Controller texture exports missing");return 0;}
    const char *names[]={"ACPVitaCross","ACPVitaCircle","ACPVitaTriangle","ACPVitaSquare","ACPVitaL","ACPVitaR","ACPVitaLeftStick","ACPVitaRightStick","ACPVitaStart","ACPVitaSelect",
        "ACPVitaBattleCross","ACPVitaBattleCircle","ACPVitaBattleSquare","ACPVitaBattleTriangle","ACPVitaBattleL","ACPVitaBattleR"};
    FILE *file=fopen("app0:ui/controller-icons.bin","rb");
    if(!file){ready=-1;acp_log("Controller icon data missing");return 0;}
    unsigned short pixels[64*64];
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
        if(fread(pixels,1,sizeof(pixels),file)!=sizeof(pixels)){fclose(file);ready=-1;return 0;}
        /* Verified geTexture object size is 12 bytes (CreateRenderTargetTexture
         * at ELF 0x574dc9). Format 0x13 is RGBA4444, one mip, no render target. */
        if(!upload(names[i],pixels)){fclose(file);ready=-1;return 0;}
    }
    fclose(file);ready=1;acp_log("Controller icons registered with game GUI: %u 64x64 RGBA4444 textures (%u KiB)",(unsigned)(sizeof(names)/sizeof(names[0])),(unsigned)(sizeof(names)/sizeof(names[0])*8));return 1;
}
