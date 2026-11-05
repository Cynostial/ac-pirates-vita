/* Launch-time copies of the save folders, keeping the last three launches.
 * The game's own .bak/.hash files cover a torn write; these cover a save that
 * is valid but wrong (for example one written after a gameplay bug). To
 * restore, copy save-backups/launch-N/ACP (and ACP_Settings) back into
 * ux0:data/acpirates/userdata with the game closed.
 * Runs before graphics start-up with one small static buffer: no heap. */
#include "acp_native.h"
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

#define USERDATA ACP_DATA_PATH "/userdata"
#define BACKUPS ACP_DATA_PATH "/save-backups"
#define KEEP 3

static char buffer[8192];

static int is_dot(const char *name){return !strcmp(name,".")||!strcmp(name,"..");}
static void remove_tree(const char *path,int depth){
    SceUID d=sceIoDopen(path);
    if(d<0){sceIoRemove(path);return;}
    SceIoDirent e;char child[256];
    for(memset(&e,0,sizeof(e));sceIoDread(d,&e)>0;memset(&e,0,sizeof(e))){
        if(is_dot(e.d_name))continue;
        if(snprintf(child,sizeof(child),"%s/%s",path,e.d_name)>=(int)sizeof(child))continue;
        if(SCE_S_ISDIR(e.d_stat.st_mode)&&depth<8)remove_tree(child,depth+1);else sceIoRemove(child);
    }
    sceIoDclose(d);sceIoRmdir(path);
}
static int copy_file(const char *from,const char *to){
    SceUID in=sceIoOpen(from,SCE_O_RDONLY,0);
    if(in<0)return -1;
    SceUID out=sceIoOpen(to,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    if(out<0){sceIoClose(in);return -1;}
    int n,result=0;
    while((n=sceIoRead(in,buffer,sizeof(buffer)))>0)if(sceIoWrite(out,buffer,n)!=n){result=-1;break;}
    if(n<0)result=-1;
    sceIoClose(in);if(sceIoClose(out)<0)result=-1;
    return result;
}
static int copy_tree(const char *from,const char *to,int *files,int depth){
    SceUID d=sceIoDopen(from);
    if(d<0||depth>8)return d<0?-1:(sceIoDclose(d),-1);
    sceIoMkdir(to,0777);
    SceIoDirent e;char a[256],b[256];int result=0;
    for(memset(&e,0,sizeof(e));sceIoDread(d,&e)>0;memset(&e,0,sizeof(e))){
        if(is_dot(e.d_name))continue;
        if(snprintf(a,sizeof(a),"%s/%s",from,e.d_name)>=(int)sizeof(a)||
           snprintf(b,sizeof(b),"%s/%s",to,e.d_name)>=(int)sizeof(b)){result=-1;continue;}
        if(SCE_S_ISDIR(e.d_stat.st_mode)){if(copy_tree(a,b,files,depth+1))result=-1;}
        else if(copy_file(a,b))result=-1;
        else (*files)++;
    }
    sceIoDclose(d);return result;
}
void acp_save_backup(void){
    SceIoStat st;
    if(sceIoGetstat(USERDATA "/ACP",&st)<0)return; /* no save yet */
    SceUInt64 start=sceKernelGetProcessTimeWide();
    sceIoMkdir(BACKUPS,0777);
    const char *temporary=BACKUPS "/launch-new";
    remove_tree(temporary,0);
    sceIoMkdir(temporary,0777);
    int files=0;
    int failed=copy_tree(USERDATA "/ACP",BACKUPS "/launch-new/ACP",&files,0);
    if(!failed&&sceIoGetstat(USERDATA "/ACP_Settings",&st)>=0)
        failed=copy_tree(USERDATA "/ACP_Settings",BACKUPS "/launch-new/ACP_Settings",&files,0);
    if(failed||!files){
        remove_tree(temporary,0);
        acp_log("Save backup: copy failed; previous launch copies kept");
        return;
    }
    char older[64],newer[64];
    snprintf(older,sizeof(older),BACKUPS "/launch-%d",KEEP);remove_tree(older,0);
    for(int i=KEEP;i>1;i--){
        snprintf(older,sizeof(older),BACKUPS "/launch-%d",i);
        snprintf(newer,sizeof(newer),BACKUPS "/launch-%d",i-1);
        sceIoRename(newer,older);
    }
    sceIoRename(temporary,BACKUPS "/launch-1");
    acp_log("Save backup: %d files copied to save-backups/launch-1 in %u us (last %d launches kept)",
            files,(unsigned)(sceKernelGetProcessTimeWide()-start),KEEP);
}
