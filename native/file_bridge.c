/* Android ARM32 file ABI and Vita path translation. Original code, MIT. */
#define _GNU_SOURCE
#include <vitasdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <wchar.h>
#include "acp_native.h"
#include "startup_cache.h"
#include "acp_loose_paths.h"
static acp_loose_paths loose_paths;
static int absent_loose_root(const char *root){
    char path[128];struct stat st;
    snprintf(path,sizeof(path),ACP_DATA_PATH "/%s",root);
    return stat(path,&st)<0&&errno==ENOENT;
}
static int missing_loose_path(const char *path){
    if(!acp_loose_paths_missing(&loose_paths,path))return 0;
    errno=ENOENT;acp_error(2);return 1;
}
extern int fndk_close(int);
extern int fndk_read(int,void*,size_t);
extern int fndk_write(int,const void*,size_t);

typedef struct {
    uint32_t p; int32_t r,w; int16_t flags,fd;
    uint32_t bf_base; int32_t bf_size,lbfsize;
    uint32_t cookie,close_fn,read_fn,seek_fn,write_fn,ext_base;
    int32_t ext_size; uint32_t up; int32_t ur; unsigned char ubuf[3],nbuf;
    uint32_t lb_base; int32_t lb_size,blksize,offset;
} AndroidFILE;
typedef struct { AndroidFILE abi; FILE *native; uint32_t magic; acp_cache_file *cache; } WrappedFILE;
static AndroidFILE standard_files[3];
_Static_assert(sizeof(AndroidFILE)==84,"Bionic ARM32 FILE ABI");
static FILE *native_file(AndroidFILE *f) {
    if (f==standard_files) return stdin;
    if (f==standard_files+1) return stdout;
    if (f==standard_files+2) return stderr;
    if (!f) return NULL;
    WrappedFILE *w=(WrappedFILE*)f;
    if (w->magic!=0x4a46494c) acp_fatal("Unknown FILE pointer passed to native stdio: %p",(void*)f);
    return w->native;
}
void acp_stdio_init(void) {
    acp_loose_paths_init(&loose_paths,absent_loose_root);
    for (int i=0;i<3;i++) { standard_files[i].flags=i?10:6; standard_files[i].fd=i; }
}
static void file_flags(AndroidFILE *f,FILE *n) {
    f->r=f->w=0;
    if (feof(n)) f->flags|=0x20; else f->flags&=~0x20;
    if (ferror(n)) f->flags|=0x40; else f->flags&=~0x40;
}
static AndroidFILE *wrap_file(FILE *n) {
    if (!n) { acp_error(acp_errno_code(errno)); return NULL; }
    WrappedFILE *w=calloc(1,sizeof(*w));
    if (!w) { fclose(n); acp_error(12); return NULL; }
    w->native=n;w->magic=0x4a46494c;w->abi.fd=fileno(n);w->abi.flags=0x12;
    return &w->abi;
}
const char *acp_path(const char *path,char buffer[1024]) {
    if (!path) { acp_error(14); return NULL; }
    if (!strcmp(path,"/proc/cpuinfo")) return "app0:cpuinfo";
    if (!strcmp(path,"/proc/meminfo")) return "app0:meminfo";
    if (!strncmp(path,"ux0:",4) || !strncmp(path,"app0:",5)) return path;
    if (!strcmp(path,"/dev/null")) return "ux0:data/acpirates/dev-null";
    /* Preserve native cwd semantics for relative filenames. Expose only
     * POSIX absolute paths back to the Android engine, which otherwise
     * interprets Vita volume names as relative paths. */
    if (*path!='/') return path;
    const char *tail=path;
    const char *prefixes[]={"/storage/emulated/0/Android/data/com.ubisoft.assassin.pirates/files","/sdcard/Android/data/com.ubisoft.assassin.pirates/files",ACP_VIRTUAL_PATH,"/sdcard"};
    for (unsigned i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++) {
        size_t length=strlen(prefixes[i]);
        if (!strncmp(path,prefixes[i],length) && (!path[length]||path[length]=='/')) {tail=path+length;break;}
    }
    while (!strncmp(tail,"./",2)) tail+=2;
    if (*tail=='/') tail++;
    int n=snprintf(buffer,1024,"%s/%s",ACP_DATA_PATH,tail);
    if (n<0||n>=1024) { acp_error(36);return NULL; }
    return buffer;
}
static AndroidFILE *jfopen(const char *p,const char *mode) {
    char b[1024];p=acp_path(p,b);if(!p)return NULL;
    if(mode[0]=='r'&&!strchr(mode,'+')){if(missing_loose_path(p))return NULL;}
    else acp_loose_paths_write(&loose_paths,p);
    FILE *n=fopen(p,mode);acp_cache_file *cache=NULL;
    if(n&&!strcmp(p,ACP_DATA_PATH "/main.253.com.ubisoft.assassin.pirates.obb")&&
       (!strcmp(mode,"r")||!strcmp(mode,"rb")))n=acp_startup_cache_file(n,&cache);
    AndroidFILE *f=wrap_file(n);if(f)((WrappedFILE*)f)->cache=cache;return f;
}
static AndroidFILE *jfdopen(int fd,const char *mode) { return wrap_file(fdopen(fd,mode)); }
static AndroidFILE *jfreopen(const char *p,const char *mode,AndroidFILE *f) {
    if (!f) return jfopen(p,mode);
    char b[1024];p=acp_path(p,b);if(!p)return NULL;
    acp_loose_paths_write(&loose_paths,p);
    FILE *old=native_file(f);
    if(f!=standard_files&&f!=standard_files+1&&f!=standard_files+2)((WrappedFILE*)f)->cache=NULL;
    FILE *n=freopen(p,mode,old);if(!n){acp_error(acp_errno_code(errno));return NULL;}
    if (f!=standard_files && f!=standard_files+1 && f!=standard_files+2) ((WrappedFILE*)f)->native=n;
    file_flags(f,n);return f;
}
static int jfclose(AndroidFILE *f) {
    if(!f)return acp_error(22);
    if(f==standard_files||f==standard_files+1||f==standard_files+2)return fflush(native_file(f));
    int r=fclose(native_file(f));((WrappedFILE*)f)->magic=0;free(f);if(r)acp_error(acp_errno_code(errno));return r;
}
static size_t jfread(void *p,size_t size,size_t count,AndroidFILE *f) { FILE *n=native_file(f);if(!n)return 0;size_t r=fread(p,size,count,n);file_flags(f,n);if(ferror(n))acp_error(acp_errno_code(errno));return r; }
static size_t jfwrite(const void *p,size_t size,size_t count,AndroidFILE *f) { FILE *n=native_file(f);if(!n)return 0;size_t r=fwrite(p,size,count,n);file_flags(f,n);if(ferror(n))acp_error(acp_errno_code(errno));return r; }
static int jfseek(AndroidFILE *f,int32_t offset,int whence) { FILE *n=native_file(f);if(!n)return acp_error(22);int r=fseek(n,offset,whence);file_flags(f,n);if(r)acp_error(acp_errno_code(errno));return r; }
static int32_t jftell(AndroidFILE *f) { return ftell(native_file(f)); }
static int jfflush(AndroidFILE *f) { return fflush(f?native_file(f):NULL); }
static int jfgetc(AndroidFILE *f) { FILE *n=native_file(f);int r=fgetc(n);file_flags(f,n);return r; }
static int jfputc(int c,AndroidFILE *f) { FILE *n=native_file(f);int r=fputc(c,n);file_flags(f,n);return r; }
static wint_t jfputwc(wchar_t c,AndroidFILE *f) { return fputwc(c,native_file(f)); }
static char *jfgets(char *p,int size,AndroidFILE *f) { FILE *n=native_file(f);char *r=fgets(p,size,n);file_flags(f,n);return r; }
static int jfputs(const char *p,AndroidFILE *f) { return fputs(p,native_file(f)); }
static int jfileno(AndroidFILE *f) {
    FILE *n=native_file(f);
    if(f!=standard_files&&f!=standard_files+1&&f!=standard_files+2){
        WrappedFILE *w=(WrappedFILE*)f;
        if(w->cache){n=acp_startup_cache_detach(n,w->cache);if(!n)return acp_error(acp_errno_code(errno));
            w->native=n;w->cache=NULL;f->fd=fileno(n);file_flags(f,n);}
    }
    return fileno(n);
}
static int jfeof(AndroidFILE *f) { return feof(native_file(f)); }
static int jferror(AndroidFILE *f) { return ferror(native_file(f)); }
static void jclearerr(AndroidFILE *f) { clearerr(native_file(f));f->flags&=~0x60; }
static void jrewind(AndroidFILE *f) { rewind(native_file(f));jclearerr(f); }
static int jungetc(int c,AndroidFILE *f) { return ungetc(c,native_file(f)); }
static int jfgetpos(AndroidFILE *f,int32_t *p) { long n=ftell(native_file(f));if(n<0)return -1;*p=n;return 0; }
static int jfsetpos(AndroidFILE *f,const int32_t *p) { return jfseek(f,*p,SEEK_SET); }
static int jsetvbuf(AndroidFILE *f,char *p,int mode,size_t n) { return setvbuf(native_file(f),p,mode,n); }
static void jsetbuf(AndroidFILE *f,char *p) { setbuf(native_file(f),p); }
static void jsetbuffer(AndroidFILE *f,char *p,size_t n) { setvbuf(native_file(f),p,p?_IOFBF:_IONBF,n); }
static void jsetlinebuf(AndroidFILE *f) { setvbuf(native_file(f),NULL,_IOLBF,0); }
static int jvfprintf(AndroidFILE *f,const char *fmt,va_list a) { return vfprintf(native_file(f),fmt,a); }
static int jfprintf(AndroidFILE *f,const char *fmt,...) { va_list a;va_start(a,fmt);int r=jvfprintf(f,fmt,a);va_end(a);return r; }
static int jvfscanf(AndroidFILE *f,const char *fmt,va_list a) { return vfscanf(native_file(f),fmt,a); }
static int jfscanf(AndroidFILE *f,const char *fmt,...) { va_list a;va_start(a,fmt);int r=jvfscanf(f,fmt,a);va_end(a);return r; }
static wint_t jgetwc(AndroidFILE *f){return getwc(native_file(f));}
static wint_t jungetwc(wint_t c,AndroidFILE *f){return ungetwc(c,native_file(f));}
static AndroidFILE *jtmpfile(void){
    char path[]="ux0:data/acpirates/userdata/cache/tmpXXXXXX";
    int fd=mkstemp(path);if(fd<0){acp_error(acp_errno_code(errno));return NULL;}
    FILE *f=fdopen(fd,"w+b");if(!f){close(fd);unlink(path);return NULL;}
    unlink(path);return wrap_file(f);
}
static int jmkstemp(char *path){
    char native[1024];const char *p=acp_path(path,native);if(!p)return -1;
    if(p!=native)snprintf(native,sizeof(native),"%s",p);
    acp_loose_paths_write(&loose_paths,native);
    int fd=mkstemp(native);if(fd<0){acp_error(acp_errno_code(errno));return -1;}
    size_t n=strlen(path),m=strlen(native);if(n>=6&&m>=6)memcpy(path+n-6,native+m-6,6);return fd;
}

static int jopen(const char *p,int flags,...) {
    int nflags=flags&3; unsigned mode=0666;
    if(flags&0100){nflags|=O_CREAT;va_list a;va_start(a,flags);mode=va_arg(a,unsigned);va_end(a);}
    if(flags&0200)nflags|=O_EXCL;
    if(flags&01000)nflags|=O_TRUNC;
    if(flags&02000)nflags|=O_APPEND;
    if(flags&04000)nflags|=O_NONBLOCK;
    char b[1024];p=acp_path(p,b);if(!p)return -1;
    if(!(nflags&(O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND))){if(missing_loose_path(p))return -1;}
    else acp_loose_paths_write(&loose_paths,p);
    int r=open(p,nflags,mode);if(r<0)acp_error(acp_errno_code(errno));return r;
}
static int jclose(int fd) { int r=fndk_close(fd);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int32_t jlseek(int fd,int32_t p,int whence) { off_t r=lseek(fd,p,whence);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int64_t jlseek64(int fd,int64_t p,int whence) { if(p>INT32_MAX||p<INT32_MIN)return acp_error(75);return jlseek(fd,(int32_t)p,whence); }
static int jread(int fd,void *p,size_t n) { int r=fndk_read(fd,p,n);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jwrite(int fd,const void *p,size_t n) { int r=fndk_write(fd,p,n);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jpread(int fd,void *p,size_t n,int32_t pos) { int r=pread(fd,p,n,pos);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jpwrite(int fd,const void *p,size_t n,int32_t pos) { int r=pwrite(fd,p,n,pos);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jpread64(int fd,void *p,size_t n,int64_t pos) { if(pos>INT32_MAX||pos<0)return acp_error(75);return jpread(fd,p,n,pos); }
static int jpwrite64(int fd,const void *p,size_t n,int64_t pos) { if(pos>INT32_MAX||pos<0)return acp_error(75);return jpwrite(fd,p,n,pos); }
static int jaccess(const char *p,int mode) { char b[1024];p=acp_path(p,b);if(!p||missing_loose_path(p))return -1;int r=access(p,mode);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jmkdir(const char *p,unsigned mode) { char b[1024];p=acp_path(p,b);if(!p)return -1;acp_loose_paths_write(&loose_paths,p);int r=mkdir(p,mode);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int junlink(const char *p) { char b[1024];p=acp_path(p,b);if(!p)return -1;int r=unlink(p);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jrmdir(const char *p) { char b[1024];p=acp_path(p,b);if(!p)return -1;int r=rmdir(p);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jremove(const char *p) { char b[1024];p=acp_path(p,b);if(!p)return -1;int r=remove(p);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jrename(const char *from,const char *to) { char a[1024],b[1024];from=acp_path(from,a);to=acp_path(to,b);if(!from||!to)return -1;acp_loose_paths_write(&loose_paths,to);int r=rename(from,to);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jchmod(const char *p,unsigned mode) { char b[1024];p=acp_path(p,b);if(!p)return -1;int r=chmod(p,mode);if(r<0)acp_error(acp_errno_code(errno));return r; }
static int jchdir(const char *p) { char b[1024];p=acp_path(p,b);if(!p)return -1;int r=chdir(p);if(r<0)acp_error(acp_errno_code(errno));return r; }
static char *virtual_path(const char *native) {
    const char *roots[]={ACP_DATA_PATH,"ux0:/data/acpirates"};
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);i++) {
        size_t length=strlen(roots[i]);
        if(!strncasecmp(native,roots[i],length) && (!native[length]||native[length]=='/')) {
            const char *tail=native+length;
            size_t bytes=strlen(ACP_VIRTUAL_PATH)+strlen(tail)+1;
            char *result=acp_malloc(bytes);
            if(!result){acp_error(12);return NULL;}
            snprintf(result,bytes,"%s%s",ACP_VIRTUAL_PATH,tail);
            return result;
        }
    }
    if(!strcmp(native,"app0:cpuinfo")||!strcmp(native,"app0:/cpuinfo"))return acp_strdup("/proc/cpuinfo");
    if(!strcmp(native,"app0:meminfo")||!strcmp(native,"app0:/meminfo"))return acp_strdup("/proc/meminfo");
    acp_log("Cannot expose native path to Android: %s",native);
    acp_error(22);return NULL;
}
static char *jgetcwd(char *p,size_t n) {
    char native[1024];
    if(!getcwd(native,sizeof(native))){acp_error(acp_errno_code(errno));return NULL;}
    char *result=virtual_path(native);if(!result)return NULL;
    size_t length=strlen(result)+1;
    if((p||n) && n<length){acp_free(result);acp_error(34);return NULL;}
    if(!p)return result;
    memcpy(p,result,length);acp_free(result);return p;
}
static char *jrealpath(const char *p,char *resolved) {
    char translated[1024];
    if (!p) { acp_error(22);return NULL; }
    if (!*p) { acp_error(2);return NULL; }
    const char *native=acp_path(p,translated);
    if (!native) return NULL;
    /* Canonicalize and check existence on Vita, then translate the result
     * back to a POSIX absolute path before the engine sees it. */
    char *canonical=realpath(native,NULL),*result=NULL;
    if (!canonical) acp_error(acp_errno_code(errno));
    else {result=virtual_path(canonical);free(canonical);}
    acp_log("realpath(%s) -> %s",p,result?result:"[missing]");
    if(result && resolved){strcpy(resolved,result);acp_free(result);return resolved;}
    return result;
}
typedef struct {
    uint64_t dev; uint8_t pad0[4];uint32_t ino32,mode,nlink,uid,gid;
    uint64_t rdev;uint8_t pad3[4];int64_t size;uint32_t blksize;uint64_t blocks;
    uint32_t atime,atimens,mtime,mtimens,ctime,ctimens;uint64_t ino;
} AndroidStat;
_Static_assert(sizeof(AndroidStat)==104 && offsetof(AndroidStat,size)==48,"Bionic ARM stat ABI");
static void stat_convert(AndroidStat *out,const struct stat *in) {
    memset(out,0,sizeof(*out));out->dev=in->st_dev;out->ino32=out->ino=in->st_ino;out->mode=in->st_mode;
    out->nlink=in->st_nlink;out->uid=in->st_uid;out->gid=in->st_gid;out->rdev=in->st_rdev;out->size=in->st_size;
    out->blksize=in->st_blksize;out->blocks=in->st_blocks;out->atime=in->st_atime;out->mtime=in->st_mtime;out->ctime=in->st_ctime;
}
static int jstat(const char *p,AndroidStat *out) { char b[1024];p=acp_path(p,b);if(!p||!out)return acp_error(14);if(missing_loose_path(p))return -1;struct stat st;int r=stat(p,&st);if(!r)stat_convert(out,&st);else acp_error(acp_errno_code(errno));return r; }
static int jfstat(int fd,AndroidStat *out) { if(!out)return acp_error(14);struct stat st;int r=fstat(fd,&st);if(!r)stat_convert(out,&st);else acp_error(acp_errno_code(errno));return r; }
typedef struct { uint64_t ino;int64_t offset;uint16_t reclen;uint8_t type;char name[256]; } AndroidDirent;
typedef struct { DIR *native;int64_t index;AndroidDirent entry; } AndroidDIR;
_Static_assert(offsetof(AndroidDirent,name)==19,"Bionic dirent ABI");
static AndroidDIR *jopendir(const char *p) { char b[1024];p=acp_path(p,b);if(!p)return NULL;DIR *n=opendir(p);if(!n){acp_error(acp_errno_code(errno));return NULL;}AndroidDIR *d=calloc(1,sizeof(*d));if(!d){closedir(n);acp_error(12);return NULL;}d->native=n;return d; }
static AndroidDirent *jreaddir(AndroidDIR *d) { if(!d)return NULL;struct dirent *n=readdir(d->native);if(!n)return NULL;memset(&d->entry,0,sizeof(d->entry));d->entry.ino=++d->index;d->entry.offset=d->index;d->entry.reclen=sizeof(d->entry);d->entry.type=SCE_S_ISDIR(n->d_stat.st_mode)?4:8;snprintf(d->entry.name,sizeof(d->entry.name),"%s",n->d_name);return &d->entry; }
static int jclosedir(AndroidDIR *d) { if(!d)return acp_error(22);int r=closedir(d->native);free(d);return r; }
static void jrewinddir(AndroidDIR *d) { rewinddir(d->native);d->index=0; }
static int jutimes(const char *p,const AcpTimeval *t) { char b[1024];p=acp_path(p,b);if(!p)return -1;struct timeval n[2];if(t){for(int i=0;i<2;i++){n[i].tv_sec=t[i].sec;n[i].tv_usec=t[i].usec;}}int r=utimes(p,t?n:NULL);if(r)acp_error(acp_errno_code(errno));return r; }
static int jstatfs(const char *p,uint32_t *out) { (void)p;if(!out)return acp_error(14);SceIoDevInfo info;int r=sceIoDevctl("ux0:",0x3001,NULL,0,&info,sizeof(info));if(r<0)return acp_error(5);memset(out,0,64);out[0]=0x4d44;out[1]=4096;out[2]=info.max_size/4096;out[3]=out[4]=info.free_size/4096;out[9]=255;out[10]=4096;return 0; }
static int junsupported(void) { return acp_error(38); }
static int jdup2(int oldfd,int newfd) {
    if (oldfd==newfd && fcntl(oldfd,F_GETFD)>=0) return newfd;
    return acp_error(38); /* Vita newlib lacks atomic descriptor replacement. */
}
static int jcreat(const char *p,unsigned mode) { return jopen(p,0100|01000|1,mode); }
static int jtruncate(const char *p,int32_t n) { char b[1024];p=acp_path(p,b);return p?truncate(p,n):-1; }
static int jftruncate64(int fd,int64_t n) { if(n<0||n>INT32_MAX)return acp_error(75);return ftruncate(fd,n); }
#define B(n,f) {n,(uintptr_t)&f}
const AcpSymbol acp_file_symbols[]={
 B("getwc",jgetwc),B("putwc",jfputwc),B("ungetwc",jungetwc),B("tmpfile",jtmpfile),B("mkstemp",jmkstemp),
 B("__sF",standard_files),B("fopen",jfopen),B("fdopen",jfdopen),B("freopen",jfreopen),B("fclose",jfclose),B("fread",jfread),B("fwrite",jfwrite),
 B("fseek",jfseek),B("fseeko",jfseek),B("ftell",jftell),B("ftello",jftell),B("fflush",jfflush),B("fgetc",jfgetc),B("getc",jfgetc),B("__srget",jfgetc),B("fputc",jfputc),B("putc",jfputc),B("__swbuf",jfputc),B("fputwc",jfputwc),B("fgets",jfgets),B("fputs",jfputs),B("fileno",jfileno),B("feof",jfeof),B("ferror",jferror),B("clearerr",jclearerr),B("rewind",jrewind),B("ungetc",jungetc),
 B("fgetpos",jfgetpos),B("fsetpos",jfsetpos),B("setvbuf",jsetvbuf),B("setbuf",jsetbuf),B("setbuffer",jsetbuffer),B("setlinebuf",jsetlinebuf),B("fprintf",jfprintf),B("vfprintf",jvfprintf),B("fscanf",jfscanf),B("vfscanf",jvfscanf),
 B("open",jopen),B("close",jclose),B("read",jread),B("write",jwrite),B("lseek",jlseek),B("lseek64",jlseek64),B("pread",jpread),B("pread64",jpread64),B("pwrite",jpwrite),B("pwrite64",jpwrite64),
 B("stat",jstat),B("stat64",jstat),B("lstat",jstat),B("lstat64",jstat),B("fstat",jfstat),B("fstat64",jfstat),B("access",jaccess),B("mkdir",jmkdir),B("rmdir",jrmdir),B("unlink",junlink),B("remove",jremove),B("rename",jrename),B("chmod",jchmod),B("chdir",jchdir),B("getcwd",jgetcwd),
 B("realpath",jrealpath),B("opendir",jopendir),B("readdir",jreaddir),B("readdir64",jreaddir),B("closedir",jclosedir),B("rewinddir",jrewinddir),B("utimes",jutimes),B("statfs",jstatfs),
 B("dup",dup),B("dup2",jdup2),B("fsync",fsync),B("fdatasync",fsync),B("ftruncate",ftruncate),B("ftruncate64",jftruncate64),B("truncate",jtruncate),B("creat",jcreat),
 B("chown",junsupported),B("lchown",junsupported),B("fchown",junsupported),B("chroot",junsupported),B("link",junsupported),B("symlink",junsupported),B("readlink",junsupported),B("fchdir",junsupported),B("flock",junsupported),
};
const size_t acp_file_symbol_count=sizeof(acp_file_symbols)/sizeof(acp_file_symbols[0]);
