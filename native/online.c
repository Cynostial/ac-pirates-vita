/* Online replacements for the game's dead services: leaderboards on the
 * configured leaderboard server instead of Google Play /
 * Facebook, and PS Vita trophies instead of Google Play achievements.
 * Everything runs on one low-priority worker thread; the game thread only
 * queues requests and reads finished results. The network is started the
 * first time a request is made, and nothing is sent without a connection. */
#include "acp_native.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/openpsid.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/io/fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "online_config.h"
#define DEFAULT_URL ACP_LEADERBOARD_URL
/* The signing key must match the configured service. */
static const char KEY[]=ACP_LEADERBOARD_KEY;
static const char *const BOARDS[]={"bounty","level","stones"};
#define BOARD_COUNT 3
#define RESULT_BYTES 4096

/* ---- SHA-256 / HMAC (FIPS 180-4) ---- */
typedef struct {uint32_t h[8];uint64_t bits;uint8_t block[64];unsigned used;} sha256;
static const uint32_t K256[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void sha256_block(sha256 *c,const uint8_t *p){
    uint32_t w[64],a,b,d,e,f,g,h,cc;
    for(int i=0;i<16;i++)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for(int i=16;i<64;i++){
        uint32_t s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3),s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for(int i=0;i<64;i++){
        uint32_t t1=h+(ROR(e,6)^ROR(e,11)^ROR(e,25))+((e&f)^(~e&g))+K256[i]+w[i];
        uint32_t t2=(ROR(a,2)^ROR(a,13)^ROR(a,22))+((a&b)^(a&cc)^(b&cc));
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha256_init(sha256 *c){
    static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->h,iv,sizeof(iv));c->bits=0;c->used=0;
}
static void sha256_add(sha256 *c,const void *data,size_t n){
    const uint8_t *p=data;c->bits+=(uint64_t)n*8;
    while(n){
        unsigned take=64-c->used;if(take>n)take=(unsigned)n;
        memcpy(c->block+c->used,p,take);c->used+=take;p+=take;n-=take;
        if(c->used==64){sha256_block(c,c->block);c->used=0;}
    }
}
static void sha256_done(sha256 *c,uint8_t out[32]){
    uint64_t bits=c->bits;uint8_t pad=0x80,zero=0,len[8];
    sha256_add(c,&pad,1);while(c->used!=56)sha256_add(c,&zero,1);
    for(int i=0;i<8;i++)len[i]=(uint8_t)(bits>>(56-8*i));
    sha256_add(c,len,8);
    for(int i=0;i<8;i++){out[4*i]=(uint8_t)(c->h[i]>>24);out[4*i+1]=(uint8_t)(c->h[i]>>16);out[4*i+2]=(uint8_t)(c->h[i]>>8);out[4*i+3]=(uint8_t)c->h[i];}
}
void acp_online_hmac_sha256(const void *key,size_t key_size,const void *msg,size_t size,uint8_t out[32]){
    uint8_t k[64]={0},pad[64],inner[32];sha256 c;
    if(key_size>64){sha256_init(&c);sha256_add(&c,key,key_size);sha256_done(&c,k);}else memcpy(k,key,key_size);
    for(int i=0;i<64;i++)pad[i]=k[i]^0x36;
    sha256_init(&c);sha256_add(&c,pad,64);sha256_add(&c,msg,size);sha256_done(&c,inner);
    for(int i=0;i<64;i++)pad[i]=k[i]^0x5c;
    sha256_init(&c);sha256_add(&c,pad,64);sha256_add(&c,inner,32);sha256_done(&c,out);
}
static void hex(const uint8_t *p,size_t n,char *out){
    static const char d[]="0123456789abcdef";
    for(size_t i=0;i<n;i++){out[2*i]=d[p[i]>>4];out[2*i+1]=d[p[i]&15];}
    out[2*n]=0;
}

/* ---- state shared with the game thread ---- */
typedef struct {int status;char text[RESULT_BYTES];} result;   /* status: 0 idle, 1 pending, 2 done, <0 error */
static struct {
    SceUID mutex,wake;int started;
    unsigned submit_pending;long long submit_value[BOARD_COUNT],submitted[BOARD_COUNT];
    unsigned fetch_pending;result fetched[BOARD_COUNT];
    int trophy_state;unsigned char trophy_pending[128];
    char name[72],console[33],url[160];
} st={.mutex=-1,.wake=-1};
static void lock(void){sceKernelLockMutex(st.mutex,1,NULL);}
static void unlock(void){sceKernelUnlockMutex(st.mutex,1);}
static int board_index(const char *board){
    for(int i=0;i<BOARD_COUNT;i++)if(board&&!strcmp(board,BOARDS[i]))return i;
    return -1;
}

/* ---- network ---- */
static int net_ready;
static int net_start(void){
    if(net_ready)return net_ready>0;
    net_ready=-1;
    if(sceSysmoduleLoadModule(SCE_SYSMODULE_NET)<0){acp_log("Online: net module unavailable");return 0;}
    SceUID block=sceKernelAllocMemBlock("ACP net",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,256*1024,NULL);
    void *memory=NULL;
    if(block<0||sceKernelGetMemBlockBase(block,&memory)<0)return 0;
    SceNetInitParam param={memory,256*1024,0};
    int r=sceNetInit(&param);
    if(r<0&&(unsigned)r!=0x80410201u){acp_log("Online: sceNetInit failed 0x%08x",r);return 0;}  /* already initialized is fine */
    sceNetCtlInit();
    if(sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP)<0){acp_log("Online: http module unavailable");return 0;}
    if(!strncmp(st.url,"https:",6)){sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);}
    r=sceHttpInit(128*1024);
    if(r<0){acp_log("Online: sceHttpInit failed 0x%08x",r);return 0;}
    net_ready=1;return 1;
}
static int connected(void){
    static int reported=-1;
    int state=0,r=sceNetCtlInetGetState(&state);
    if(r<0)state=-2;
    if(state!=reported){reported=state;acp_log("Online: network state %d (0x%08x)",state,(unsigned)r);}
    return state==SCE_NETCTL_STATE_CONNECTED;
}
/* One request; the response body (first RESULT_BYTES-1 bytes) goes to out. */
static int http(const char *url,const char *body,char *out,size_t out_size){
    int tmpl=sceHttpCreateTemplate("ACPiratesVita/1",SCE_HTTP_VERSION_1_1,SCE_TRUE);
    if(tmpl<0)return tmpl;
    sceHttpSetResolveTimeOut(tmpl,5000000);sceHttpSetConnectTimeOut(tmpl,5000000);
    sceHttpSetSendTimeOut(tmpl,5000000);sceHttpSetRecvTimeOut(tmpl,8000000);
    int status=-1,conn=sceHttpCreateConnectionWithURL(tmpl,url,SCE_FALSE),req=-1;
    if(conn>=0)req=sceHttpCreateRequestWithURL(conn,body?SCE_HTTP_METHOD_POST:SCE_HTTP_METHOD_GET,url,body?strlen(body):0);
    if(req>=0){
        if(body)sceHttpAddRequestHeader(req,"Content-Type","application/x-www-form-urlencoded",SCE_HTTP_HEADER_OVERWRITE);
        int r=sceHttpSendRequest(req,body,body?strlen(body):0);
        if(r>=0)r=sceHttpGetStatusCode(req,&status);
        size_t n=0;
        while(r>=0&&n+1<out_size){
            r=sceHttpReadData(req,out+n,out_size-1-n);
            if(r<=0)break;
            n+=(size_t)r;
        }
        out[n]=0;
        if(r<0)status=r;
        sceHttpDeleteRequest(req);
    }else status=req;
    if(conn>=0)sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tmpl);
    return status;
}
static size_t urlencode(const char *s,char *out,size_t cap){
    static const char d[]="0123456789ABCDEF";size_t n=0;
    for(;*s&&n+4<cap;s++){
        unsigned char c=(unsigned char)*s;
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out[n++]=(char)c;
        else{out[n++]='%';out[n++]=d[c>>4];out[n++]=d[c&15];}
    }
    out[n]=0;return n;
}

/* ---- identity ---- */
static void identity(void){
    SceAppUtilInitParam init;SceAppUtilBootParam boot;
    memset(&init,0,sizeof(init));memset(&boot,0,sizeof(boot));
    sceAppUtilInit(&init,&boot);
    if(sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME,(SceChar8 *)st.name,sizeof(st.name)-1)<0||!st.name[0])
        strcpy(st.name,"Pirate");
    SceKernelOpenPsId id;uint8_t digest[32];
    memset(&id,0,sizeof(id));sceKernelGetOpenPsId(&id);
    acp_online_hmac_sha256("acpirates-console",17,&id,sizeof(id),digest);
    hex(digest,16,st.console);
    strcpy(st.url,DEFAULT_URL);
    SceUID fd=sceIoOpen(ACP_DATA_PATH "/leaderboard-url.txt",SCE_O_RDONLY,0);
    if(fd>=0){
        char buf[sizeof(st.url)-1];int n=sceIoRead(fd,buf,sizeof(buf)-1);  /* room for an added "/" */sceIoClose(fd);
        if(n>0){
            buf[n]=0;char *e=buf+strcspn(buf,"\r\n \t");*e=0;
            if(!strncmp(buf,"http://",7)||!strncmp(buf,"https://",8)){
                snprintf(st.url,sizeof(st.url),"%s%s",buf,buf[strlen(buf)-1]=='/'?"":"/");
            }
        }
    }
}

/* ---- leaderboard jobs (worker thread) ---- */
static void submit(int board,long long value){
    char msg[256],sig_hex[65],name[160],body[600];uint8_t sig[32];
    long long ts=(long long)time(NULL);
    int n=snprintf(msg,sizeof(msg),"%s\n%lld\n%s\n%s\n%lld",BOARDS[board],value,st.name,st.console,ts);
    acp_online_hmac_sha256(KEY,strlen(KEY),msg,(size_t)n,sig);hex(sig,32,sig_hex);
    urlencode(st.name,name,sizeof(name));
    snprintf(body,sizeof(body),"v=1&board=%s&value=%lld&name=%s&console=%s&ts=%lld&sig=%s",
        BOARDS[board],value,name,st.console,ts,sig_hex);
    char url[200],reply[128];snprintf(url,sizeof(url),"%ssubmit.php",st.url);
    int status=http(url,body,reply,sizeof(reply));
    reply[strcspn(reply,"\r\n")]=0;
    acp_log("Leaderboard: %s %lld -> HTTP %d \"%s\"",BOARDS[board],value,status,reply);
    if(status==200&&!strncmp(reply,"OK",2)){lock();if(value>st.submitted[board])st.submitted[board]=value;unlock();}
}
static void fetch(int board){
    static char text[RESULT_BYTES];
    char url[260];snprintf(url,sizeof(url),"%stop.php?board=%s&limit=8&console=%s",st.url,BOARDS[board],st.console);
    int status=http(url,NULL,text,sizeof(text));
    acp_log("Leaderboard: fetch %s -> HTTP %d (%u bytes)",BOARDS[board],status,(unsigned)strlen(text));
    lock();
    if(status==200&&!strncmp(text,"ACPLB ",6)){st.fetched[board].status=2;memcpy(st.fetched[board].text,text,sizeof(text));}
    else{st.fetched[board].status=status<0?-2:-3;st.fetched[board].text[0]=0;}
    unlock();
}

/* ---- trophies (worker thread) ---- */
extern int acp_trophy_start(void);          /* trophies.c: 1 ready, -1 unavailable */
extern int acp_trophy_unlock(int id);

static int worker(SceSize args,void *argp){
    (void)args;(void)argp;
    identity();
    int trophies=acp_trophy_start();
    lock();st.trophy_state=trophies;unlock();
    acp_log("Online: player \"%s\", server %s, trophies %s",st.name,st.url,trophies>0?"ready":"unavailable");
    for(;;){
        sceKernelWaitSema(st.wake,1,NULL);
        for(;;){
            int job=-1,kind=0;long long value=0;
            lock();
            for(int i=0;i<128&&job<0;i++)if(st.trophy_pending[i]){st.trophy_pending[i]=0;job=i;kind=3;}
            for(int i=0;i<BOARD_COUNT&&job<0;i++)if(st.submit_pending&(1u<<i)){st.submit_pending&=~(1u<<i);job=i;kind=1;value=st.submit_value[i];}
            for(int i=0;i<BOARD_COUNT&&job<0;i++)if(st.fetch_pending&(1u<<i)){st.fetch_pending&=~(1u<<i);job=i;kind=2;}
            unlock();
            if(job<0)break;
            if(kind==3){if(st.trophy_state>0)acp_trophy_unlock(job);continue;}
            if(!st.url[0]||!KEY[0]||!net_start()||!connected()){
                lock();if(kind==2){st.fetched[job].status=-1;st.fetched[job].text[0]=0;}unlock();
                continue;
            }
            if(kind==1)submit(job,value);else fetch(job);
        }
    }
    return 0;
}
static void start(void){
    if(st.started)return;
    st.started=1;
    st.mutex=sceKernelCreateMutex("ACP online",0,0,NULL);
    st.wake=sceKernelCreateSema("ACP online wake",0,0,1024,NULL);
    SceUID thread=sceKernelCreateThread("ACP online",worker,0x10000100+24,0x8000,0,0,NULL);
    if(st.mutex<0||st.wake<0||thread<0||sceKernelStartThread(thread,0,NULL)<0){st.started=-1;acp_log("Online: worker start failed");}
}

/* ---- game-thread API (vita_scripts.c bindings) ---- */
void acp_online_start(void){start();}
const char *acp_online_user_name(void){return st.name[0]?st.name:"Pirate";}
/* Best values only: a lower or equal value than already accepted is not sent. */
void acp_online_submit(const char *board,long long value){
    int i=board_index(board);
    start();
    if(i<0||value<0||st.started<0)return;
    lock();
    int send=value>st.submitted[i]&&(!(st.submit_pending&(1u<<i))||value>st.submit_value[i]);
    if(send){st.submit_value[i]=value;st.submit_pending|=1u<<i;}
    unlock();
    if(send)sceKernelSignalSema(st.wake,1);
}
void acp_online_fetch(const char *board){
    int i=board_index(board);
    start();
    if(i<0||st.started<0)return;
    lock();st.fetched[i].status=1;st.fetch_pending|=1u<<i;unlock();
    sceKernelSignalSema(st.wake,1);
}
/* Copies the latest result; returns its status (see result). */
int acp_online_result(const char *board,char *out,size_t size){
    int i=board_index(board);
    if(i<0||st.started<=0){if(size)out[0]=0;return -4;}
    lock();int status=st.fetched[i].status;
    if(size){strncpy(out,st.fetched[i].text,size-1);out[size-1]=0;}
    unlock();return status;
}
void acp_online_trophy(int id){
    start();
    if(id<1||id>127||st.started<0)return;
    lock();st.trophy_pending[id]=1;unlock();
    sceKernelSignalSema(st.wake,1);
}
/* 0 while starting, 1 trophies work, -1 unavailable (no NoTrpDrm plugin). */
int acp_online_trophy_state(void){
    start();
    if(st.started<=0)return 0;
    lock();int s=st.trophy_state;unlock();return s;
}
