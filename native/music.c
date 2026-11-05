/* Android MediaPlayer subset used by AudioPlayerViewController.
 * 64 logical players; decoder/PCM storage exists only during playback.
 * Preparing the catalog probes one header at a time, never whole tracks.
 * The original game's OpenAL mixer continues through OpenSL on the BGM port.
 * This separate MAIN port mixes streamed music at 48 kHz. MIT. */
#define _GNU_SOURCE
#include "acp_native.h"
#include "acp_jni_runtime.h"
#include <mpg123.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#define PLAYERS 64
#define FRAMES 4096
typedef struct {
    mpg123_handle *decoder;
    short pcm[FRAMES*2],previous[2],next[2];
    size_t frames,cursor;
    int primed;
    off_t chunk_start,next_sample;
} Stream;
typedef struct {
    int occupied,fd,playing,looping,channels,prepared,eof;
    int gain[2],rate;
    int64_t begin,length,position;
    off_t sample;unsigned phase;
    Stream *stream;
    char name[48]; /* file name for the music log; empty for archive slices */
} Player;
static Player players[PLAYERS];
static SceUID mutex=-1,worker=-1;
static int port=-1;
static void lock(void){sceKernelLockMutex(mutex,1,NULL);}
static void unlock(void){sceKernelUnlockMutex(mutex,1);}
static ssize_t source_read(void *ctx,void *out,size_t bytes){
    Player *p=ctx;
    if(p->position>=p->length)return 0;
    if(bytes>65536)bytes=65536;
    if((int64_t)bytes>p->length-p->position)bytes=p->length-p->position;
    ssize_t n=pread(p->fd,out,bytes,(off_t)(p->begin+p->position));
    if(n>0)p->position+=n;
    return n;
}
static off_t source_seek(void *ctx,off_t offset,int whence){
    Player *p=ctx;int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?p->position:whence==SEEK_END?p->length:-1;
    if(base<0||base+offset<0||base+offset>p->length)return -1;
    p->position=base+offset;return p->position;
}
static void close_decoder(mpg123_handle *d){if(d){mpg123_close(d);mpg123_delete(d);}}
static void drop_stream(Player *p){
    if(p->stream){close_decoder(p->stream->decoder);free(p->stream);p->stream=NULL;}
}
static void reset(Player *p){
    drop_stream(p);
    if(p->fd>=0)close(p->fd);
    int occupied=p->occupied;
    memset(p,0,sizeof(*p));p->occupied=occupied;p->fd=-1;p->gain[0]=p->gain[1]=32768;
}
static const char *open_decoder(Player *p,mpg123_handle **out){
    int status;*out=NULL;p->position=0;
    mpg123_handle *d=mpg123_new(NULL,&status);
    if(!d)return "MP3 decoder allocation failed";
    const char *error="Invalid MP3 stream";
    if(mpg123_param(d,MPG123_ADD_FLAGS,MPG123_FORCE_STEREO,0)!=MPG123_OK||
       /* A positive index size is fixed, unlike the auto-growing negative mode. */
       mpg123_param(d,MPG123_INDEX_SIZE,128,0)!=MPG123_OK||
       mpg123_param(d,MPG123_RESYNC_LIMIT,65536,0)!=MPG123_OK||
       mpg123_format_none(d)!=MPG123_OK)goto failed;
    const long *rates;size_t count;mpg123_rates(&rates,&count);
    for(size_t i=0;i<count;i++)if(mpg123_format(d,rates[i],MPG123_STEREO,MPG123_ENC_SIGNED_16)!=MPG123_OK)goto failed;
    if(mpg123_replace_reader_handle(d,source_read,source_seek,NULL)!=MPG123_OK||
       mpg123_open_handle(d,p)!=MPG123_OK)goto failed;
    long rate;int channels,encoding;
    if(mpg123_getformat(d,&rate,&channels,&encoding)!=MPG123_OK)goto failed;
    error="Unsupported MP3 output format";
    if(encoding!=MPG123_ENC_SIGNED_16||channels!=2||rate<8000||rate>48000)goto failed;
    if(p->prepared&&(p->rate!=rate||p->channels!=channels))goto failed;
    p->rate=(int)rate;p->channels=channels;*out=d;return NULL;
failed:
    close_decoder(d);p->position=0;return error;
}
static const char *open_stream(Player *p){
    if(p->stream)return NULL;
    Stream *s=calloc(1,sizeof(*s));if(!s)return "MP3 playback buffer allocation failed";
    const char *error=open_decoder(p,&s->decoder);
    if(!error&&p->sample&&mpg123_seek(s->decoder,p->sample,SEEK_SET)!=p->sample)error="MP3 resume seek failed";
    if(error){close_decoder(s->decoder);free(s);return error;}
    p->stream=s;return NULL;
}
static int next_frame(Player *p,short sample[2],off_t *index){
    Stream *s=p->stream;
    if(s->cursor>=s->frames){
        size_t bytes=0;
        int result=mpg123_read(s->decoder,(unsigned char*)s->pcm,sizeof(s->pcm),&bytes);
        if(result==MPG123_NEW_FORMAT)return 0; /* fixed format after prepare */
        s->frames=bytes/(sizeof(short)*p->channels);s->cursor=0;
        if(!s->frames&&p->looping&&mpg123_seek(s->decoder,0,SEEK_SET)>=0){
            result=mpg123_read(s->decoder,(unsigned char*)s->pcm,sizeof(s->pcm),&bytes);
            s->frames=bytes/(sizeof(short)*p->channels);
        }
        if(!s->frames)return 0;
        if(result!=MPG123_OK&&result!=MPG123_DONE)return 0;
        s->chunk_start=mpg123_tell(s->decoder)-(off_t)s->frames;
    }
    *index=s->chunk_start+(off_t)s->cursor;
    sample[0]=s->pcm[s->cursor*p->channels];
    sample[1]=p->channels==1?sample[0]:s->pcm[s->cursor*2+1];
    s->cursor++;return 1;
}
static void finish(Player *p){p->playing=0;p->eof=1;p->sample=0;p->phase=0;drop_stream(p);}
static void mix(short output[1024*2]){
    int mixed[1024*2]={0};lock();
    for(unsigned i=0;i<PLAYERS;i++){
        Player *p=players+i;Stream *s=p->stream;if(!p->playing||!s)continue;
        if(!s->primed){
            if(!next_frame(p,s->previous,&p->sample)||!next_frame(p,s->next,&s->next_sample)){finish(p);continue;}
            s->primed=1;
        }
        for(unsigned frame=0;frame<1024&&p->playing;frame++){
            for(unsigned channel=0;channel<2;channel++){
                int value=s->previous[channel]+(int)(((int64_t)s->next[channel]-s->previous[channel])*p->phase/48000);
                mixed[frame*2+channel]+=(int)((int64_t)value*p->gain[channel]/32768);
            }
            p->phase+=p->rate;
            while(p->phase>=48000){
                p->phase-=48000;memcpy(s->previous,s->next,sizeof(s->next));p->sample=s->next_sample;
                if(!next_frame(p,s->next,&s->next_sample)){finish(p);break;}
            }
        }
    }
    unlock();
    for(unsigned i=0;i<2048;i++)output[i]=mixed[i]>32767?32767:mixed[i]<-32768?-32768:mixed[i];
}
static int music_thread(SceSize size,void *args){
    (void)size;(void)args;
    short output[1024*2];
    for(;;){
        mix(output);
        if(sceAudioOutOutput(port,output)<0){acp_log("Music audio output failed");return -1;}
    }
}
static int start_worker(void){
    if(worker>=0)return 0;
    port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,1024,48000,SCE_AUDIO_OUT_MODE_STEREO);
    if(port<0)return -1;
    int volume[2]={32767,32767};sceAudioOutSetVolume(port,SCE_AUDIO_VOLUME_FLAG_L_CH|SCE_AUDIO_VOLUME_FLAG_R_CH,volume);
    worker=sceKernelCreateThread("AC streamed MP3",music_thread,0x10000100,65536,0,0,NULL);
    if(worker<0){sceAudioOutReleasePort(port);port=-1;return -1;}
    int result=sceKernelStartThread(worker,0,NULL);
    if(result<0){sceKernelDeleteThread(worker);worker=-1;sceAudioOutReleasePort(port);port=-1;return -1;}
    acp_log("MP3 playback worker started: 48000 Hz stereo");
    return 0;
}
static void exception(JNIEnv *e,const char *message){
    acp_log("MediaPlayer: %s",message);
    jclass c=(*e)->FindClass(e,"java/io/IOException");
    if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}
}
enum {NEW,FD_NEW,SOURCE_FD,SOURCE_PATH,PREPARE,START,PAUSE,STOP,RESET,RELEASE,LOOP,VOLUME,STREAM,IS_PLAYING};
static jvalue call(JNIEnv *e,jobject object,const jvalue *args,unsigned argc,void *ctx){
    (void)argc;jvalue result={0};unsigned op=(unsigned)(uintptr_t)ctx;
    if(op==FD_NEW)return result;
    Player *p=NULL;
    if(op!=NEW)p=acp_jni_runtime_data(e,object,"android/media/MediaPlayer");
    if(op!=NEW&&!p){exception(e,"Released MediaPlayer");return result;}
    int source_fd=-1;int64_t offset=0,length=0;
    if(op==SOURCE_FD){
        jclass c=(*e)->GetObjectClass(e,args[0].l);
        jfieldID f=c?(*e)->GetFieldID(e,c,"descriptor","I"):NULL;
        int fd=f?(*e)->GetIntField(e,args[0].l,f):-1;
        (*e)->DeleteLocalRef(e,c);offset=args[1].j;length=args[2].j;
        struct stat st;
        if(fd<0||offset<0||length<=0||fstat(fd,&st)||offset>st.st_size||length>st.st_size-offset){exception(e,"Invalid MP3 archive slice");return result;}
        source_fd=dup(fd);
    } else if(op==SOURCE_PATH){
        const char *s=(*e)->GetStringUTFChars(e,args[0].l,NULL);if(!s)return result;
        char translated[1024];const char *path=acp_path(s,translated);
        if(path)source_fd=open(path,O_RDONLY);
        (*e)->ReleaseStringUTFChars(e,args[0].l,(char*)s);
        struct stat st;if(source_fd>=0){if(fstat(source_fd,&st)){close(source_fd);source_fd=-1;}else length=st.st_size;}
    }
    if((op==SOURCE_FD||op==SOURCE_PATH)&&source_fd<0){exception(e,"Cannot open MP3 source");return result;}
    const char *error=NULL;char name[48]="";
    if(op==SOURCE_PATH&&source_fd>=0){
        const char *s=(*e)->GetStringUTFChars(e,args[0].l,NULL);
        if(s){const char *base=strrchr(s,'/');snprintf(name,sizeof(name),"%s",base?base+1:s);(*e)->ReleaseStringUTFChars(e,args[0].l,(char*)s);}
    }
    lock();
    int was_playing=p&&op!=NEW&&p->playing;
    switch(op){
    case NEW:
        for(unsigned i=0;i<PLAYERS;i++)if(!players[i].occupied){p=players+i;memset(p,0,sizeof(*p));p->occupied=1;p->fd=-1;p->gain[0]=p->gain[1]=32768;break;}
        if(!p)error="Logical MediaPlayer capacity exhausted";
        break;
    case SOURCE_FD:case SOURCE_PATH:
        reset(p);p->fd=source_fd;p->begin=offset;p->length=length;memcpy(p->name,name,sizeof(name));break;
    case PREPARE:{
        if(p->fd<0){error="No MP3 source";break;}
        if(p->prepared)break;
        mpg123_handle *probe=NULL;error=open_decoder(p,&probe);close_decoder(probe);p->position=0;
        if(error)break;
        p->prepared=1;acp_log("MP3 prepared without retained decoder: %d Hz, %lld bytes on storage",p->rate,(long long)p->length);break;
    }
    case START:
        if(!p->prepared){error="MP3 start before prepare";break;}
        if(p->eof){p->sample=0;p->phase=0;p->eof=0;}
        error=open_stream(p);if(error)break;
        if(start_worker()){drop_stream(p);error="Cannot open Vita music audio port";break;}
        p->playing=1;break;
    case PAUSE:p->playing=0;drop_stream(p);break;
    case STOP:
        p->playing=0;drop_stream(p);p->sample=0;p->phase=0;p->eof=0;break;
    case RESET:reset(p);break;
    case RELEASE:reset(p);p->occupied=0;break;
    case LOOP:p->looping=args[0].z!=0;break;
    case VOLUME:
        for(unsigned i=0;i<2;i++){float x=args[i].f;if(!isfinite(x)||x<0)x=0;if(x>1)x=1;p->gain[i]=(int)(x*32768);}break;
    case STREAM:if(args[0].i!=3)error="Only STREAM_MUSIC is supported";break;
    case IS_PLAYING:result.z=p->playing!=0;break;
    }
    /* Track changes are rare: log them to show what plays when. */
    char logged[48]="";int loop=0;
    if(p&&op!=NEW){memcpy(logged,p->name,sizeof(logged));loop=p->looping;}
    unlock();
    if(op==START&&!error)acp_log("Music start: %s loop=%d",logged[0]?logged:"(archive)",loop);
    else if((op==STOP||op==PAUSE)&&was_playing)acp_log("Music %s: %s",op==STOP?"stop":"pause",logged[0]?logged:"(archive)");
    if(op==NEW&&p)acp_jni_runtime_set_data(e,object,"android/media/MediaPlayer",p);
    if(op==RELEASE)acp_jni_runtime_set_data(e,object,"android/media/MediaPlayer",NULL);
    if(error)exception(e,error);
    return result;
}
int acp_music_configure(acp_jni_runtime *r){
    mutex=sceKernelCreateMutex("AC MP3 streams",0,0,NULL);
    if(mutex<0||mpg123_init()!=MPG123_OK)return -1;
    const char *classes[]={"android/media/MediaPlayer","android/media/AudioManager","java/io/FileDescriptor"};
    for(unsigned i=0;i<3;i++)if(acp_jni_runtime_class(r,classes[i],"java/lang/Object"))return -1;
    if(acp_jni_runtime_exception_class(r,"java/io/IOException","java/lang/Exception"))return -1;
    const acp_jni_field_def fields[]={{"java/io/FileDescriptor","descriptor","I",0},{"android/media/AudioManager","STREAM_MUSIC","I",1}};
    if(acp_jni_runtime_fields(r,fields,2))return -1;
#define M(n,s,op) {"android/media/MediaPlayer",n,s,0,call,(void*)(uintptr_t)op}
    const acp_jni_method_def methods[]={
        M("<init>","()V",NEW),M("setDataSource","(Ljava/io/FileDescriptor;JJ)V",SOURCE_FD),
        M("setDataSource","(Ljava/lang/String;)V",SOURCE_PATH),M("prepare","()V",PREPARE),
        M("start","()V",START),M("stop","()V",STOP),M("pause","()V",PAUSE),M("reset","()V",RESET),
        M("release","()V",RELEASE),M("setVolume","(FF)V",VOLUME),M("setLooping","(Z)V",LOOP),
        M("setAudioStreamType","(I)V",STREAM),M("isPlaying","()Z",IS_PLAYING),
        {"java/io/FileDescriptor","<init>","()V",0,call,(void*)(uintptr_t)FD_NEW}};
    return acp_jni_runtime_methods(r,methods,sizeof(methods)/sizeof(*methods));
}
