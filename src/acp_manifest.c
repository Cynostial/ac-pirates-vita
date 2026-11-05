#include "acp_manifest.h"
#include "acp_platform.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRIES "acp/ManifestEntries"
#define KEYS "acp/ManifestKeys"
#define LIST "java/util/List"
#define VECTOR "java/util/Vector"
#define JAR "java/util/jar/JarFile"
#define MANIFEST "java/util/jar/Manifest"
#define APPINFO "android/content/pm/ApplicationInfo"
enum { APP_INFO,JAR_CTOR,JAR_MANIFEST,JAR_CLOSE,MANIFEST_ENTRIES,MAP_KEYS,COUNT,
       VECTOR_CTOR,SUBLIST,TO_ARRAY,GET };
typedef struct {const char *cls,*name,*signature;unsigned op;} method;
static const method methods[]={
    {"android/content/Context","getApplicationInfo","()Landroid/content/pm/ApplicationInfo;",APP_INFO},
    {JAR,"<init>","(Ljava/lang/String;)V",JAR_CTOR},
    {JAR,"getManifest","()Ljava/util/jar/Manifest;",JAR_MANIFEST},
    {JAR,"close","()V",JAR_CLOSE},
    {MANIFEST,"getEntries","()Ljava/util/Map;",MANIFEST_ENTRIES},
    {ENTRIES,"keySet","()Ljava/util/Set;",MAP_KEYS},
    {ENTRIES,"size","()I",COUNT},{KEYS,"size","()I",COUNT},
    {VECTOR,"<init>","(Ljava/util/Collection;)V",VECTOR_CTOR},
    {LIST,"subList","(II)Ljava/util/List;",SUBLIST},
    {LIST,"toArray","([Ljava/lang/Object;)[Ljava/lang/Object;",TO_ARRAY},
    {LIST,"size","()I",COUNT},{LIST,"get","(I)Ljava/lang/Object;",GET}
};
/* Each method's context carries both its immutable operation and index. */
typedef struct {acp_manifest *index;unsigned op;} dispatch;
static dispatch contexts[sizeof(methods)/sizeof(*methods)];

static void raise(JNIEnv *e,const char *name,const char *message) {
    jclass c=(*e)->FindClass(e,name);
    if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}
}
static int range(JNIEnv *e,jobject object,jint *begin,jint *end,int write) {
    jclass c=(*e)->FindClass(e,LIST);
    if(!c)return -1;
    jfieldID b=(*e)->GetFieldID(e,c,"begin","I"),n=(*e)->GetFieldID(e,c,"end","I");
    if(b&&n) {
        if(write){(*e)->SetIntField(e,object,b,*begin);(*e)->SetIntField(e,object,n,*end);}
        else{*begin=(*e)->GetIntField(e,object,b);*end=(*e)->GetIntField(e,object,n);}
    }
    (*e)->DeleteLocalRef(e,c);
    return (*e)->ExceptionCheck(e)?-1:0;
}
static int is_list(JNIEnv *e,jobject object) {
    jclass c=(*e)->FindClass(e,LIST);
    int yes=c&&(*e)->IsInstanceOf(e,object,c);
    (*e)->DeleteLocalRef(e,c);return yes;
}
static jvalue call(JNIEnv *e,jobject receiver,const jvalue *a,unsigned argc,void *ctx) {
    (void)argc;dispatch *d=ctx;acp_manifest *m=d->index;jvalue out={0};
    jint begin=0,end=(jint)m->count;
    switch(d->op) {
    case APP_INFO: {
        out.l=acp_jni_runtime_object(e,APPINFO,m);
        jclass c=(*e)->FindClass(e,APPINFO);
        jfieldID f=c?(*e)->GetFieldID(e,c,"sourceDir","Ljava/lang/String;"):NULL;
        jstring path=(*e)->NewStringUTF(e,ACP_DATA "game.apk");
        if(out.l&&f&&path)(*e)->SetObjectField(e,out.l,f,path);
        (*e)->DeleteLocalRef(e,path);(*e)->DeleteLocalRef(e,c);break;
    }
    case JAR_CTOR: {
        const char *path=(*e)->GetStringUTFChars(e,a[0].l,NULL);
        if(!path)break;
        int valid=!strcmp(path,ACP_DATA "game.apk");
        (*e)->ReleaseStringUTFChars(e,a[0].l,(char*)path);
        if(valid)acp_jni_runtime_set_data(e,receiver,JAR,m);
        else raise(e,"java/lang/IllegalArgumentException","JarFile path is outside the packaged APK");
        break;
    }
    case JAR_MANIFEST:
        if(acp_jni_runtime_data(e,receiver,JAR)==m)out.l=acp_jni_runtime_object(e,MANIFEST,m);
        else raise(e,"java/lang/IllegalStateException","JarFile is closed");
        break;
    case JAR_CLOSE:acp_jni_runtime_set_data(e,receiver,JAR,NULL);break;
    case MANIFEST_ENTRIES:out.l=acp_jni_runtime_object(e,ENTRIES,m);break;
    case MAP_KEYS:out.l=acp_jni_runtime_object(e,KEYS,m);break;
    case VECTOR_CTOR:
        if(acp_jni_runtime_data(e,a[0].l,KEYS)!=m)break;
        if(!acp_jni_runtime_set_data(e,receiver,VECTOR,m))range(e,receiver,&begin,&end,1);
        break;
    case COUNT:
        if(is_list(e,receiver)&&range(e,receiver,&begin,&end,0))break;
        out.i=end-begin;break;
    case SUBLIST:
        if(range(e,receiver,&begin,&end,0))break;
        if(a[0].i<0||a[1].i<a[0].i||a[1].i>end-begin){
            raise(e,"java/lang/IndexOutOfBoundsException","subList range");break;
        }
        end=begin+a[1].i;begin+=a[0].i;
        out.l=acp_jni_runtime_object(e,LIST,m);
        if(out.l)range(e,out.l,&begin,&end,1);
        break;
    case GET:
        if(range(e,receiver,&begin,&end,0))break;
        if(a[0].i<0||a[0].i>=end-begin)raise(e,"java/lang/IndexOutOfBoundsException","List index");
        else out.l=(*e)->NewStringUTF(e,m->entry[begin+a[0].i]);
        break;
    case TO_ARRAY: {
        if(range(e,receiver,&begin,&end,0))break;
        jsize length=(*e)->GetArrayLength(e,a[0].l);
        if((*e)->ExceptionCheck(e))break;
        /* The engine passes String[32] (or the final shorter batch).
         * Preserve the caller's component type if a larger array is needed. */
        out.l=a[0].l;
        if(length<end-begin){
            out.l=acp_jni_runtime_array_like(e,a[0].l,end-begin);
            if(!out.l)break;
        }
        for(jint i=begin;i<end&&!(*e)->ExceptionCheck(e);i++){
            jstring name=(*e)->NewStringUTF(e,m->entry[i]);
            if(name)(*e)->SetObjectArrayElement(e,out.l,i-begin,name);
            (*e)->DeleteLocalRef(e,name);
        }
        if(length>end-begin)(*e)->SetObjectArrayElement(e,out.l,end-begin,NULL);
        if(end==(jint)m->count&&!(*e)->ExceptionCheck(e)&&m->status)
            m->status("All APK manifest entries delivered to the game");
        break;
    }
    }
    return out;
}
static uint32_t u32(const unsigned char *p){
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int load(acp_manifest *m,const char *path){
    FILE *f=fopen(path,"rb");if(!f)return -1;
    unsigned char header[12];
    if(fread(header,1,12,f)!=12||memcmp(header,"ACM1",4)){fclose(f);return -1;}
    m->count=u32(header+4);m->bytes=u32(header+8);
    if(!m->count||m->count>8192||!m->bytes||m->bytes>1024*1024){fclose(f);return -1;}
    m->names=malloc(m->bytes);m->entry=calloc(m->count,sizeof(*m->entry));
    if(!m->names||!m->entry){fclose(f);return -1;}
    size_t got=fread(m->names,1,m->bytes,f);int extra=fgetc(f),bad=ferror(f);fclose(f);
    if(got!=m->bytes||extra!=EOF||bad)return -1;
    size_t pos=0;
    for(unsigned i=0;i<m->count;i++){
        if(pos>=m->bytes)return -1;
        char *end=memchr(m->names+pos,0,m->bytes-pos);
        if(!end||end==m->names+pos||(size_t)(end-m->names-pos)>=1024)return -1;
        m->entry[i]=m->names+pos;
        for(const unsigned char *p=(const unsigned char*)m->entry[i];*p;p++)if(*p<32||*p>126)return -1;
        pos=(size_t)(end-m->names)+1;
    }
    return pos==m->bytes?0:-1;
}
void acp_manifest_dispose(acp_manifest *m){
    if(m){free(m->names);free(m->entry);memset(m,0,sizeof(*m));}
}
int acp_manifest_configure(acp_manifest *m,acp_jni_runtime *r,const char *path){
    if(!m||!r||!path)return -1;
    memset(m,0,sizeof(*m));
    if(load(m,path)){acp_manifest_dispose(m);return -1;}
    const char *classes[][2]={
        {APPINFO,"java/lang/Object"},{JAR,"java/lang/Object"},{MANIFEST,"java/lang/Object"},
        {ENTRIES,"java/util/Map"},{KEYS,"java/util/Set"},
        {LIST,"java/util/Collection"},{VECTOR,LIST}
    };
    for(unsigned i=0;i<sizeof(classes)/sizeof(*classes);i++)
        if(acp_jni_runtime_class(r,classes[i][0],classes[i][1]))return -1;
    const acp_jni_field_def fields[]={
        {APPINFO,"sourceDir","Ljava/lang/String;",0},{LIST,"begin","I",0},{LIST,"end","I",0}
    };
    if(acp_jni_runtime_fields(r,fields,3))return -1;
    for(unsigned i=0;i<sizeof(methods)/sizeof(*methods);i++){
        contexts[i]=(dispatch){m,methods[i].op};
        acp_jni_method_def def={methods[i].cls,methods[i].name,methods[i].signature,0,call,contexts+i};
        if(acp_jni_runtime_methods(r,&def,1))return -1;
    }
    return 0;
}
