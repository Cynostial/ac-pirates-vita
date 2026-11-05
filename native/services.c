#include "acp_native.h"
#include "memory_budget.h"
#include "acp_platform.h"
#include <psp2/io/devctl.h>
#include <string.h>
#include <limits.h>
#include <time.h>
typedef struct {const char *cls,*name,*sig;int is_static;} Service;
#define M(c,n,s,t) {c,n,s,t},
#define N(c,n,s)
static const Service methods[]={
#include "services.inc"
    {"android/view/InputDevice","getDeviceIds","()[I",1},
    {"java/lang/System","currentTimeMillis","()J",1},
    {"java/lang/System","getProperty","(Ljava/lang/String;)Ljava/lang/String;",1},
};
#undef M
#undef N
static acp_platform *platform;
static acp_jni_registry *registry;
static jvalue call(JNIEnv *e,jobject receiver,const jvalue *a,unsigned argc,void *ctx){
    (void)argc;const Service *s=ctx;jvalue r={0};const char *text=NULL;
    if(!strcmp(s->cls,"org/ubisoft/geea/spark2/DeviceJava")){
        if(!strcmp(s->name,"GetTotalMemory"))r.i=512; /* Java returns MiB, verified with REA. */
        else if(!strcmp(s->name,"GetAvailableMemory")){
            size_t used,peak;acp_memory_usage(&used,&peak);r.i=(ACP_GAME_HEAP_BYTES-used)/(1024*1024);
        } else if(!strcmp(s->name,"GetDevicePpi"))r.f=220;
        else if(!strcmp(s->name,"GetDeviceScreenSize")){
            jint dimensions[2]={960,544};r.l=(*e)->NewIntArray(e,2);
            if(r.l)(*e)->SetIntArrayRegion(e,r.l,0,2,dimensions);
        } else if(!strcmp(s->name,"GetCacheDir"))text=ACP_DATA "userdata/cache";
        else if(!strcmp(s->name,"GetDevicePreferredLanguage"))text="en";
        else if(!strcmp(s->name,"GetDeviceCountryCode"))text="US";
        else if(!strcmp(s->name,"GetInternetConnectionType"))text="NONE";
        else if(!strcmp(s->name,"GetPlatformArg")){
            const char *key=(*e)->GetStringUTFChars(e,a[0].l,NULL);
            if(key){for(unsigned i=0;i<platform->pairs;i++)if(!strcmp(key,platform->keys[i])){text=platform->values[i];break;}
                (*e)->ReleaseStringUTFChars(e,a[0].l,(char*)key);}
        } else if(strstr(s->name,"StorageFree")||strstr(s->name,"StorageTotal")){
            SceIoDevInfo info;
            if(sceIoDevctl("ux0:",0x3001,NULL,0,&info,sizeof(info))<0)r.i=-1;
            else {uint64_t n=(strstr(s->name,"Free")?info.free_size:info.max_size)/(1024*1024);r.i=n>INT_MAX?INT_MAX:(jint)n;}
        } else if(!strcmp(s->name,"GetLatLng"))r.l=(*e)->NewDoubleArray(e,2);
        else if(!strcmp(s->name,"HasPermission")||!strcmp(s->name,"CheckGyroscopeAvailable"))r.z=JNI_TRUE;
        /* Offline/location/vibration/store/share services have no provider.
         * Their capability/status booleans stay false; no success callback. */
    } else if(!strcmp(s->cls,"android/view/InputDevice")){
        r.l=(*e)->NewIntArray(e,0); /* Touch and gamepad events arrive via AInputQueue. */
    } else if(!strcmp(s->cls,"java/lang/System")){
        if(!strcmp(s->name,"currentTimeMillis"))r.j=(jlong)time(NULL)*1000;
        else{
            const char *key=(*e)->GetStringUTFChars(e,a[0].l,NULL);
            if(key){
                if(!strcmp(key,"os.arch"))text="armv7l";
                else if(!strcmp(key,"os.name"))text="PlayStation Vita";
                else if(!strcmp(key,"user.language"))text="en";
                else if(!strcmp(key,"http.agent"))text="ACPirates/2.9.1 (PlayStation Vita)";
                (*e)->ReleaseStringUTFChars(e,a[0].l,(char*)key);
            }
        }
    } else if(!strcmp(s->cls,"org/ubisoft/geea/spark2/MoPubJava")){
        const char *callback=NULL;
        if(!strcmp(s->name,"LoadMoPubInterstitial")||!strcmp(s->name,"ShowMoPubInterstitial"))callback="OnInterstitialDidFail";
        if(!strcmp(s->name,"LoadMoPubRewardedVideo")||!strcmp(s->name,"ShowMoPubRewardedVideo"))callback="OnVideoAdDidFail";
        if(callback){
            void (*f)(JNIEnv*,jobject,jstring)=(void*)acp_jni_resolve(registry,s->cls,callback,"(Ljava/lang/String;)V");
            if(f)f(e,receiver,a[0].l);
        }
    }
    /* Optional analytics/ad IDs are absent; use empty strings, never tokens
     * or purchase success. Object/array methods above return actual objects. */
    if(!text&&!strcmp(strchr(s->sig,')')+1,"Ljava/lang/String;"))text="";
    if(text)r.l=(*e)->NewStringUTF(e,text);
    return r;
}
int acp_services_configure(acp_platform *p,acp_jni_registry *n){
    platform=p;registry=n;acp_jni_runtime *r=p->runtime;
    const char *classes[]={
        "org/ubisoft/geea/spark2/DeviceJava","org/ubisoft/geea/spark2/AndroidInputJava",
        "org/ubisoft/geea/spark2/MoPubJava","org/ubisoft/geea/spark2/MSDKJava",
        "org/ubisoft/geea/spark2/UpsightJava","org/ubisoft/geea/spark2/HasOfferJava",
        "org/ubisoft/geea/spark2/MagnetJava","android/view/InputDevice",
        "android/os/Build","android/os/Build$VERSION","java/lang/System"};
    for(unsigned i=0;i<sizeof(classes)/sizeof(*classes);i++)
        if(acp_jni_runtime_class(r,classes[i],"java/lang/Object"))return -1;
#define M(c,n,s,t)
#define N(c,nm,s) if(acp_jni_declare(n,c,nm,s))return -1;
#include "services.inc"
#undef M
#undef N
    for(unsigned i=0;i<sizeof(methods)/sizeof(*methods);i++){
        acp_jni_method_def m={methods[i].cls,methods[i].name,methods[i].sig,methods[i].is_static,call,(void*)(methods+i)};
        if(acp_jni_runtime_methods(r,&m,1))return -1;
    }
    const acp_jni_field_def fields[]={
#define STRING(n) {"android/os/Build",n,"Ljava/lang/String;",1}
        STRING("MODEL"),STRING("MANUFACTURER"),STRING("BRAND"),STRING("DEVICE"),STRING("BOARD"),
        STRING("PRODUCT"),STRING("HARDWARE"),STRING("FINGERPRINT"),STRING("CPU_ABI"),STRING("CPU_ABI2"),STRING("DISPLAY"),
#undef STRING
        {"android/os/Build$VERSION","SDK_INT","I",1},
        {"android/os/Build$VERSION","RELEASE","Ljava/lang/String;",1},
        {"android/os/Build$VERSION","SDK","Ljava/lang/String;",1}};
    return acp_jni_runtime_fields(r,fields,sizeof(fields)/sizeof(*fields));
}
int acp_services_values(JNIEnv *e){
    const char *fields[][2]={{"MODEL","PS Vita"},{"MANUFACTURER","Sony"},{"BRAND","Sony"},{"DEVICE","vita"},{"BOARD","vita"},
        {"PRODUCT","vita"},{"HARDWARE","vita"},{"FINGERPRINT","vita/acpirates/2.9.1"},{"CPU_ABI","armeabi-v7a"},{"CPU_ABI2","armeabi"},{"DISPLAY","PS Vita"}};
    jclass c=(*e)->FindClass(e,"android/os/Build");
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++){
        jfieldID f=(*e)->GetStaticFieldID(e,c,fields[i][0],"Ljava/lang/String;");
        jstring value=(*e)->NewStringUTF(e,fields[i][1]);(*e)->SetStaticObjectField(e,c,f,value);(*e)->DeleteLocalRef(e,value);
    }
    (*e)->DeleteLocalRef(e,c);c=(*e)->FindClass(e,"android/os/Build$VERSION");
    jfieldID f=(*e)->GetStaticFieldID(e,c,"SDK_INT","I");(*e)->SetStaticIntField(e,c,f,19);
    const char *names[]={"RELEASE","SDK"},*values[]={"4.4","19"};
    for(unsigned i=0;i<2;i++){f=(*e)->GetStaticFieldID(e,c,names[i],"Ljava/lang/String;");
        jstring value=(*e)->NewStringUTF(e,values[i]);(*e)->SetStaticObjectField(e,c,f,value);(*e)->DeleteLocalRef(e,value);}
    (*e)->DeleteLocalRef(e,c);c=(*e)->FindClass(e,"android/media/AudioManager");
    f=(*e)->GetStaticFieldID(e,c,"STREAM_MUSIC","I");(*e)->SetStaticIntField(e,c,f,3);(*e)->DeleteLocalRef(e,c);
    return (*e)->ExceptionCheck(e)?-1:0;
}
