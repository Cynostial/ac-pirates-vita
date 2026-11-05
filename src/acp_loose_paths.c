#include "acp_loose_paths.h"
#include <string.h>
static const char *roots[]={"Spark2Engine","Spark2Projects"};
static unsigned root_bit(const char *path){
    if(!path)return 0;
    const char *prefixes[]={"ux0:data/acpirates/","ux0:/data/acpirates/"};
    const char *tail=0;
    for(unsigned i=0;i<2;i++)if(!strncmp(path,prefixes[i],strlen(prefixes[i]))){tail=path+strlen(prefixes[i]);break;}
    if(!tail)return 0;
    /* Do not shortcut paths whose traversal could leave the absent root. */
    for(const char *p=tail;*p;p++)if((p==tail||p[-1]=='/')&&p[0]=='.'&&
       (p[1]=='/'||!p[1]||(p[1]=='.'&&(p[2]=='/'||!p[2]))))return 0;
    for(unsigned i=0;i<2;i++){
        size_t n=strlen(roots[i]);
        if(!strncmp(tail,roots[i],n)&&(!tail[n]||tail[n]=='/'))return 1u<<i;
    }
    return 0;
}
void acp_loose_paths_init(acp_loose_paths *p,int (*missing)(const char *)){
    unsigned absent=0;
    for(unsigned i=0;i<2;i++)if(missing(roots[i]))absent|=1u<<i;
    __atomic_store_n(&p->absent,absent,__ATOMIC_RELEASE);
}
int acp_loose_paths_missing(const acp_loose_paths *p,const char *path){
    return (__atomic_load_n(&p->absent,__ATOMIC_ACQUIRE)&root_bit(path))!=0;
}
void acp_loose_paths_write(acp_loose_paths *p,const char *path){
    /* Save/cache writes cannot populate either loose asset directory. */
    if(path&&!strstr(path,"..")&&
       (!strncmp(path,"ux0:data/acpirates/userdata/",28)||
        !strncmp(path,"ux0:/data/acpirates/userdata/",29)))return;
    /* Unknown/traversal paths can modify either root; conservatively clear. */
    unsigned bit=root_bit(path);
    if(!bit)bit=3;
    __atomic_fetch_and(&p->absent,~bit,__ATOMIC_ACQ_REL);
}
