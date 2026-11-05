#include "acp_native.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* bit 0: inverted camera; bit 1: FPS overlay; bit 2: performance mode;
 * bit 3: aim assist; bit 4: no "trophies unavailable" notice */
static unsigned preferences=14;
unsigned acp_preferences_get(void){return __atomic_load_n(&preferences,__ATOMIC_RELAXED);}
void acp_preferences_load(void){
    FILE *f=fopen(ACP_DATA_PATH "/vita-settings.txt","rb");
    if(!f)f=fopen(ACP_DATA_PATH "/vita-settings.tmp","rb");
    if(!f)return;
    char text[64]={0};size_t n=fread(text,1,sizeof(text)-1,f);fclose(f);
    unsigned invert,fps,perf=1,aim=1,quiet=0;int used=0;
    /* 00.58 stored the same bit as "reflect"; older records have no field
     * and take the performance default. Records before 00.67 have no aim
     * field and take the aim-assist default. */
    if(sscanf(text,"ACP1 invert=%u fps=%u perf=%u aim=%u notrophynote=%u\n%n",&invert,&fps,&perf,&aim,&quiet,&used)==5&&(size_t)used==n){}
    else if((used=0,quiet=0,sscanf(text,"ACP1 invert=%u fps=%u perf=%u aim=%u\n%n",&invert,&fps,&perf,&aim,&used)==4)&&(size_t)used==n){}
    else if((used=0,aim=1,sscanf(text,"ACP1 invert=%u fps=%u perf=%u\n%n",&invert,&fps,&perf,&used)==3||
        (used=0,sscanf(text,"ACP1 invert=%u fps=%u reflect=%u\n%n",&invert,&fps,&perf,&used)==3))&&(size_t)used==n){}
    else{used=0;perf=1;if(sscanf(text,"ACP1 invert=%u fps=%u\n%n",&invert,&fps,&used)!=2||(size_t)used!=n)return;}
    if(invert<=1&&fps<=1&&perf<=1&&aim<=1&&quiet<=1)
        __atomic_store_n(&preferences,invert|(fps<<1)|(perf<<2)|(aim<<3)|(quiet<<4),__ATOMIC_RELAXED);
}
int acp_preferences_set(unsigned bit,unsigned value){
    if(bit>4||value>1)return -1;
    unsigned next=(acp_preferences_get()&~(1u<<bit))|(value<<bit);
    if(next==acp_preferences_get())return 0;
    const char *temp=ACP_DATA_PATH "/vita-settings.tmp",*path=ACP_DATA_PATH "/vita-settings.txt";
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    int ok=fprintf(f,"ACP1 invert=%u fps=%u perf=%u aim=%u notrophynote=%u\n",next&1,(next>>1)&1,(next>>2)&1,(next>>3)&1,(next>>4)&1)>0;
    if(fflush(f)||fsync(fileno(f)))ok=0;
    if(fclose(f))ok=0;
    if(ok){
        /* Vita rename does not replace an existing destination. The complete
         * temporary record is also recoverable at startup if interrupted. */
        remove(path);
        if(rename(temp,path)==0){__atomic_store_n(&preferences,next,__ATOMIC_RELAXED);return 0;}
    }else remove(temp);
    acp_log("Could not save Vita settings");return -1;
}
