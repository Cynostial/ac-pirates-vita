#include "acp_assets.h"
#include <string.h>
#include <limits.h>

int acp_assets_init(acp_assets *m,const char *root,const acp_asset_io *io){
    if(!m||!root||!*root||!io||!io->lock||!io->unlock||!io->open||
       !io->read||!io->seek||!io->close)return -1;
    size_t n=strlen(root);if(n>=sizeof(m->root)||root[n-1]!='/')return -1;
    memset(m,0,sizeof(*m));m->io=*io;memcpy(m->root,root,n+1);return 0;
}
static int valid(acp_assets *m,acp_asset *a){
    uintptr_t p=(uintptr_t)a,begin=(uintptr_t)m->handles,end=(uintptr_t)(m->handles+ACP_ASSET_HANDLES);
    return p>=begin&&p<end&&(p-begin)%sizeof(*a)==0&&a->active;
}
static int path_safe(const char *s){
    if(!s||!*s||*s=='/'||*s=='\\')return 0;
    const char *component=s;
    for(const char *p=s;;p++){
        if(*p=='\\'||*p==':')return 0;
        if(!*p||*p=='/'){
            size_t n=(size_t)(p-component);
            if(!n||(n==1&&component[0]=='.')||(n==2&&component[0]=='.'&&component[1]=='.'))return 0;
            if(!*p)return 1;
            component=p+1;
        }
    }
}
acp_asset *acp_asset_open(acp_assets *m,const char *name){
    if(!m||!path_safe(name))return NULL;
    char path[768];size_t root=strlen(m->root),tail=strlen(name);
    if(tail>=sizeof(path)-root)return NULL;
    memcpy(path,m->root,root);memcpy(path+root,name,tail+1);
    m->io.lock(m->io.ctx);
    acp_asset *a=NULL;
    for(unsigned i=0;i<ACP_ASSET_HANDLES;i++)if(!m->handles[i].active){a=m->handles+i;break;}
    if(a){
        uint64_t size=0;int fd=m->io.open(m->io.ctx,path,&size);
        if(fd<0)a=NULL;
        else if(size>INT32_MAX){m->io.close(m->io.ctx,fd);a=NULL;}
        else *a=(acp_asset){fd,1,(uint32_t)size,0};
    }
    m->io.unlock(m->io.ctx);return a;
}
int acp_asset_read(acp_assets *m,acp_asset *a,void *dest,size_t count){
    if(!m||(!dest&&count)||count>INT32_MAX)return -1;
    m->io.lock(m->io.ctx);int result=-1;
    if(valid(m,a)){
        size_t remaining=a->size-a->position;
        if(count>remaining)count=remaining;
        size_t done=0;
        while(done<count){
            unsigned chunk=(unsigned)(count-done>256u*1024?256u*1024:count-done);
            int n=m->io.read(m->io.ctx,a->fd,(unsigned char*)dest+done,chunk);
            if(n<0){result=done?(int)done:-1;goto finish;}
            if((unsigned)n>chunk){result=-1;goto finish;}
            if(!n)break;
            done+=(unsigned)n;a->position+=(unsigned)n;
        }
        result=(int)done;
    }
finish:
    m->io.unlock(m->io.ctx);return result;
}
int32_t acp_asset_seek(acp_assets *m,acp_asset *a,int32_t offset,int whence){
    if(!m)return -1;
    m->io.lock(m->io.ctx);int32_t result=-1;
    if(valid(m,a)&&(whence==0||whence==1||whence==2)){
        int64_t pos=(int64_t)offset+(whence==1?a->position:whence==2?a->size:0);
        /* Android asset streams reject seeking outside their bounded region. */
        if(pos>=0&&pos<=a->size&&m->io.seek(m->io.ctx,a->fd,pos)==pos){
            a->position=(uint32_t)pos;result=(int32_t)pos;
        }
    }
    m->io.unlock(m->io.ctx);return result;
}
int32_t acp_asset_length(acp_assets *m,acp_asset *a,int remaining){
    if(!m)return -1;
    m->io.lock(m->io.ctx);
    int32_t result=valid(m,a)?(int32_t)(a->size-(remaining?a->position:0)):-1;
    m->io.unlock(m->io.ctx);return result;
}
int acp_asset_close(acp_assets *m,acp_asset *a){
    if(!m)return -1;
    m->io.lock(m->io.ctx);int result=-1;
    if(valid(m,a)){result=m->io.close(m->io.ctx,a->fd);if(!result)memset(a,0,sizeof(*a));}
    m->io.unlock(m->io.ctx);return result;
}
int acp_assets_idle(acp_assets *m){
    if(!m)return 0;
    m->io.lock(m->io.ctx);int idle=1;
    for(unsigned i=0;i<ACP_ASSET_HANDLES;i++)if(m->handles[i].active)idle=0;
    m->io.unlock(m->io.ctx);return idle;
}
