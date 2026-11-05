/* Bounded OpenSL ES mixer. Vita permits one BGM port per process, shared by
 * the game's OpenAL and Bink engines. No allocations in the output loop. */
#include "sles_allinclusive.h"
#include <vitasdk.h>
#include <pthread.h>
extern void acp_log(const char *, ...);
extern void acp_fatal(const char *, ...);
#define ACP_SL_ENGINES 4
#define ACP_SL_FRAMES 512
#define ACP_SL_SAMPLES (ACP_SL_FRAMES*2)
typedef struct { IEngine *engine; unsigned readers; int closing,nonzero; } AcpOutput;
static AcpOutput outputs[ACP_SL_ENGINES];
static pthread_mutex_t output_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t output_changed=PTHREAD_COND_INITIALIZER;
static SceUID output_worker=-1;
static int output_closing,output_stop;
static int16_t output_pcm[2][ACP_SL_SAMPLES],output_scratch[ACP_SL_SAMPLES];
static int32_t output_sum[ACP_SL_SAMPLES];
static int output_frequency(void){
    return (&_opensles_user_freq!=NULL&&_opensles_user_freq>0)?_opensles_user_freq:44100;
}
static int output_thread(unsigned argc,void *arg){
    (void)argc;(void)arg;
    int frequency=output_frequency();
    int port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,ACP_SL_FRAMES,frequency,SCE_AUDIO_OUT_MODE_STEREO);
    int volumes[2]={32767,32767};
    if(port>=0){
        int result=sceAudioOutSetVolume(port,SCE_AUDIO_VOLUME_FLAG_L_CH|SCE_AUDIO_VOLUME_FLAG_R_CH,volumes);
        if(result<0){sceAudioOutReleasePort(port);port=result;}
    }
    if(port<0)acp_log("OpenSLES: shared port failed %08x; consuming queues on clock",(unsigned)port);
    else acp_log("OpenSLES: shared BGM port %d, %d Hz, four engine slots",port,frequency);
    unsigned index=0;
    uint64_t deadline=sceKernelGetProcessTimeWide();
    const unsigned period=(ACP_SL_FRAMES*1000000u+(unsigned)frequency/2)/(unsigned)frequency;
    while(!__atomic_load_n(&output_stop,__ATOMIC_ACQUIRE)){
        memset(output_sum,0,sizeof(output_sum));
        for(unsigned slot=0;slot<ACP_SL_ENGINES;slot++){
            pthread_mutex_lock(&output_mutex);
            AcpOutput *output=&outputs[slot];
            IEngine *engine=output->closing?NULL:output->engine;
            if(engine)output->readers++;
            pthread_mutex_unlock(&output_mutex);
            if(!engine)continue;
            memset(output_scratch,0,sizeof(output_scratch));
            COutputMix *mix=engine->mOutputMix;
            if(mix)IOutputMixExt_FillBuffer(&mix->mOutputMixExt.mItf,output_scratch,sizeof(output_scratch));
            for(unsigned i=0;i<ACP_SL_SAMPLES;i++)output_sum[i]+=output_scratch[i];
            if(!output->nonzero)for(unsigned i=0;i<ACP_SL_SAMPLES;i++)if(output_scratch[i]){
                output->nonzero=1;
                acp_log("OpenSLES: first nonzero PCM engine=%p shared port=%d sample=%d",engine,port,output_scratch[i]);break;
            }
            pthread_mutex_lock(&output_mutex);
            output->readers--;pthread_cond_broadcast(&output_changed);
            pthread_mutex_unlock(&output_mutex);
        }
        int16_t *pcm=output_pcm[index];index^=1;
        for(unsigned i=0;i<ACP_SL_SAMPLES;i++){
            int32_t value=output_sum[i];pcm[i]=(int16_t)(value>32767?32767:value<-32768?-32768:value);
        }
        if(port>=0){
            int result=sceAudioOutOutput(port,pcm);
            if(result<0){
                acp_log("OpenSLES: output failed %08x; consuming queues on clock",(unsigned)result);
                sceAudioOutReleasePort(port);port=-1;deadline=sceKernelGetProcessTimeWide();
            }else continue;
        }
        /* Even an unavailable device must not leave Bink waiting forever for
         * its buffer completion callbacks. Keep their normal audio cadence. */
        deadline+=period;
        uint64_t now=sceKernelGetProcessTimeWide();
        if(deadline>now)sceKernelDelayThread((unsigned)(deadline-now));
        else deadline=now;
    }
    if(port>=0)sceAudioOutReleasePort(port);
    return 0;
}
int acp_sl_open_engine(IEngine *engine){
    if(!engine)return -1;
    pthread_mutex_lock(&output_mutex);
    while(output_closing)pthread_cond_wait(&output_changed,&output_mutex);
    AcpOutput *output=NULL;
    for(unsigned i=0;i<ACP_SL_ENGINES;i++){
        if(outputs[i].engine==engine){int result=outputs[i].closing?-1:0;pthread_mutex_unlock(&output_mutex);return result;}
        if(!outputs[i].engine&&!output)output=&outputs[i];
    }
    if(!output){pthread_mutex_unlock(&output_mutex);acp_log("OpenSLES: four engine slots occupied");return -1;}
    memset(output,0,sizeof(*output));output->engine=engine;
    if(output_worker<0){
        __atomic_store_n(&output_stop,0,__ATOMIC_RELEASE);
        output_worker=sceKernelCreateThread("ACP OpenSLES",output_thread,0x10000100,0x10000,0,0,NULL);
        int result=output_worker<0?output_worker:sceKernelStartThread(output_worker,0,NULL);
        if(result<0){
            acp_log("OpenSLES: worker creation failed: %08x",(unsigned)result);
            if(output_worker>=0)sceKernelDeleteThread(output_worker);
            output_worker=-1;memset(output,0,sizeof(*output));
            pthread_mutex_unlock(&output_mutex);return -1;
        }
    }
    pthread_mutex_unlock(&output_mutex);return 0;
}
void SDL_open(IEngine *engine){
    if(acp_sl_open_engine(engine)<0)acp_fatal("OpenSLES: cannot create shared audio worker");
}
void acp_sl_close_engine(IEngine *engine){
    pthread_mutex_lock(&output_mutex);
    for(unsigned i=0;i<ACP_SL_ENGINES;i++)if(outputs[i].engine==engine){
        AcpOutput *output=&outputs[i];
        if(output->closing)break;
        output->closing=1;
        while(output->readers)pthread_cond_wait(&output_changed,&output_mutex);
        memset(output,0,sizeof(*output));break;
    }
    pthread_mutex_unlock(&output_mutex);
}
void SDL_close(void){
    pthread_mutex_lock(&output_mutex);
    while(output_closing)pthread_cond_wait(&output_changed,&output_mutex);
    output_closing=1;__atomic_store_n(&output_stop,1,__ATOMIC_RELEASE);
    SceUID thread=output_worker;
    pthread_mutex_unlock(&output_mutex);
    if(thread>=0){sceKernelWaitThreadEnd(thread,NULL,NULL);sceKernelDeleteThread(thread);}
    pthread_mutex_lock(&output_mutex);
    memset(outputs,0,sizeof(outputs));output_worker=-1;output_closing=0;
    pthread_cond_broadcast(&output_changed);pthread_mutex_unlock(&output_mutex);
}
