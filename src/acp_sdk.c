#define _POSIX_C_SOURCE 200809L
#include "acp_sdk.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define UTILS "ubisoft/mobile/mobileSDK/Utils"
#define AUTH "ubisoft/mobile/mobileAuth/MobileAuth"
#define UDID "org/OpenUDID/OpenUDID_manager"
#define FLURRY "com/flurry/android/FlurryAgent"
enum {NOTHING,ACTIVITY,SET_ACTIVITY,ACTIVITY_CLASS,ACTIVITY_PATH,VERSION,COUNTRY,LANGUAGE,EMPTY,DATE,
      INSTALL_ID,READY,PREPARE,LOG,UUID_NEW,UUID_TEXT,TIMEZONE_NEW,TIMEZONE_ID,KEY_GET,
      KEY_SET,KEY_REMOVE,USER_AGENT,TIMESTAMP,CRYPTO_UNAVAILABLE,EXTERNAL_PATH,PROXY_PORT};
static void error(JNIEnv *e,const char *type,const char *text){
    jclass c=(*e)->FindClass(e,type);if(c){(*e)->ThrowNew(e,c,text);(*e)->DeleteLocalRef(e,c);}
}
int acp_sdk_uuid(const unsigned char bytes[16],char text[37]){
    if(!bytes||!text)return -1;
    static const char hex[]="0123456789abcdef";unsigned at=0;
    for(unsigned i=0;i<16;i++){
        if(i==4||i==6||i==8||i==10)text[at++]='-';
        unsigned b=bytes[i];if(i==6)b=(b&15)|64;if(i==8)b=(b&63)|128;
        text[at++]=hex[b>>4];text[at++]=hex[b&15];
    }
    text[at]=0;return 0;
}
static int valid_uuid(const char *s){
    if(!s||strlen(s)!=36)return 0;
    for(unsigned i=0;i<36;i++){
        if(i==8||i==13||i==18||i==23){if(s[i]!='-')return 0;}
        else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    }
    return 1;
}
typedef struct {const char *cls,*name,*sig;unsigned op;int is_static;} method;
#define S(c,n,s,o) {c,n,s,o,1}
#define I(c,n,s,o) {c,n,s,o,0}
static const method methods[]={
    /* The native SDK sets this even when the analytics service is disabled.
     * Offline compatibility: accept the call without storing or sending IDs. */
    S(FLURRY,"setUserId","(Ljava/lang/String;)V",NOTHING),
    S(UTILS,"SetGameActivity","(Landroid/app/Activity;I)V",SET_ACTIVITY),
    S(UTILS,"GetGameActivity","()Landroid/app/Activity;",ACTIVITY),
    S(UTILS,"GetAppContext","()Landroid/content/Context;",ACTIVITY),
    S(UTILS,"GetGameActivityClass","(Landroid/content/Context;)Ljava/lang/Class;",ACTIVITY_CLASS),
    S(UTILS,"GetGameActivityJniPath","()Ljava/lang/String;",ACTIVITY_PATH),
    S(UTILS,"getDeviceGameVersion","()Ljava/lang/String;",VERSION),
    S(UTILS,"getCountry","()Ljava/lang/String;",COUNTRY),S(UTILS,"getLanguage","()Ljava/lang/String;",LANGUAGE),
    S(UTILS,"getMNC","()Ljava/lang/String;",EMPTY),S(UTILS,"getMCC","()Ljava/lang/String;",EMPTY),
    S(UTILS,"getDeviceMac","()Ljava/lang/String;",EMPTY),S(UTILS,"getProxyName","()Ljava/lang/String;",EMPTY),
    S(UTILS,"getProxyPort","()I",PROXY_PORT),S(UTILS,"isAndroidTv","()Z",NOTHING),
    S(UTILS,"getDeviceDateTime","()Ljava/lang/String;",DATE),S(UTILS,"getTimezone","()Ljava/lang/String;",TIMEZONE_ID),
    S(UTILS,"getExternalStoragePath","()Ljava/lang/String;",EXTERNAL_PATH),
    S(UTILS,"PrepareFile","(Ljava/lang/String;Ljava/lang/String;)V",PREPARE),
    S(UTILS,"Log","(ILjava/lang/String;)V",LOG),
    S(UTILS,"OnMsdkPause","()V",NOTHING),S(UTILS,"OnMsdkResume","()V",NOTHING),
    S(UDID,"sync","(Landroid/content/Context;)V",NOTHING),S(UDID,"isInitialized","()Z",READY),
    S(UDID,"getOpenUDID","()Ljava/lang/String;",INSTALL_ID),
    S(AUTH,"getUDUID","()Ljava/lang/String;",INSTALL_ID),S(AUTH,"getUserName","()Ljava/lang/String;",EMPTY),
    S(AUTH,"getUserAgent","()Ljava/lang/String;",USER_AGENT),S(AUTH,"getProxy","(Ljava/lang/String;)Ljava/lang/String;",EMPTY),
    S(AUTH,"getTimeStamp","(J)Ljava/lang/String;",TIMESTAMP),
    S(AUTH,"keyChainGetDataForKey","(Ljava/lang/String;)Ljava/lang/String;",KEY_GET),
    S(AUTH,"keyChainSetValueForKey","(Ljava/lang/String;Ljava/lang/String;)Z",KEY_SET),
    S(AUTH,"keyChainRemoveValueForKey","(Ljava/lang/String;)Z",KEY_REMOVE),
    S("java/util/UUID","randomUUID","()Ljava/util/UUID;",UUID_NEW),
    I("java/util/UUID","toString","()Ljava/lang/String;",UUID_TEXT),
    S("java/util/TimeZone","getDefault","()Ljava/util/TimeZone;",TIMEZONE_NEW),
    I("java/util/TimeZone","getID","()Ljava/lang/String;",TIMEZONE_ID),
    /* Native MobileAuth caches these IDs even offline. A crypto operation is
     * explicitly unavailable; no digest, signature or authentication succeeds. */
    S("javax/crypto/Mac","getInstance","(Ljava/lang/String;)Ljavax/crypto/Mac;",CRYPTO_UNAVAILABLE),
    I("javax/crypto/Mac","init","(Ljava/security/Key;)V",CRYPTO_UNAVAILABLE),
    I("javax/crypto/Mac","doFinal","([B)[B",CRYPTO_UNAVAILABLE),
    I("javax/crypto/spec/SecretKeySpec","<init>","([BLjava/lang/String;)V",CRYPTO_UNAVAILABLE),
};
#undef S
#undef I
/* Each runtime is process-scoped, as are the existing platform service tables. */
static acp_sdk *sdk;
static jvalue call(JNIEnv *e,jobject receiver,const jvalue *a,unsigned argc,void *ctx){
    (void)argc;const method *m=ctx;acp_sdk *s=sdk;jvalue result={0};const char *text=NULL;char buffer[64];
    switch(m->op){
    case NOTHING:break;
    case READY:result.z=JNI_TRUE;break;
    case SET_ACTIVITY:case ACTIVITY:{
        jclass c=(*e)->FindClass(e,UTILS);jfieldID f=(*e)->GetStaticFieldID(e,c,"s_activity","Landroid/app/Activity;");
        if(f){
            if(m->op==SET_ACTIVITY)(*e)->SetStaticObjectField(e,c,f,a[0].l);
            else result.l=(*e)->GetStaticObjectField(e,c,f);
        }
        (*e)->DeleteLocalRef(e,c);break;
    }
    case ACTIVITY_CLASS:result.l=(*e)->FindClass(e,ACP_ACTIVITY);break;
    case ACTIVITY_PATH:text=ACP_ACTIVITY;break;
    case VERSION:text="2.9.1";break;
    case COUNTRY:text="US";break;
    case LANGUAGE:text="en";break;
    case EMPTY:text="";break;
    case INSTALL_ID:text=s->id;break;
    case EXTERNAL_PATH:text=ACP_DATA "userdata";break;
    case USER_AGENT:text="ACPirates/2.9.1 (PlayStation Vita)";break;
    case PROXY_PORT:result.i=-1;break;
    case TIMEZONE_ID:text="UTC";break;
    case TIMEZONE_NEW:result.l=acp_jni_runtime_object(e,"java/util/TimeZone",NULL);break;
    case DATE:case TIMESTAMP:{
        time_t now=m->op==DATE?time(NULL):(time_t)(a[0].j/1000);struct tm parts;
        if(!gmtime_r(&now,&parts)){error(e,"java/lang/IllegalArgumentException","Invalid timestamp");break;}
        strftime(buffer,sizeof(buffer),m->op==DATE?"%Y-%m-%d %H:%M:%S":"%Y-%m-%dT%H:%MZ",&parts);text=buffer;break;
    }
    case KEY_GET:case KEY_SET:case KEY_REMOVE:{
        const char *key=(*e)->GetStringUTFChars(e,a[0].l,NULL);if(!key)break;
        /* Only the locally persisted installation identity has a provider.
         * Unknown account/token records stay absent; mutations report failure. */
        if(m->op==KEY_GET&&!strcmp(key,"ubiduid"))text=s->id;
        (*e)->ReleaseStringUTFChars(e,a[0].l,(char *)key);break;
    }
    case UUID_NEW:{
        unsigned char bytes[16];
        if(s->ops.random(s->ops.ctx,bytes,sizeof(bytes))||acp_sdk_uuid(bytes,buffer)){
            error(e,"java/lang/IllegalStateException","Random generator failed");break;
        }
        result.l=acp_jni_runtime_object(e,"java/util/UUID",NULL);
        jclass c=(*e)->FindClass(e,"java/util/UUID");jfieldID f=(*e)->GetFieldID(e,c,"value","Ljava/lang/String;");
        jstring value=(*e)->NewStringUTF(e,buffer);
        if(result.l&&f&&value)(*e)->SetObjectField(e,result.l,f,value);
        (*e)->DeleteLocalRef(e,value);(*e)->DeleteLocalRef(e,c);break;
    }
    case UUID_TEXT:{
        jclass c=(*e)->GetObjectClass(e,receiver);jfieldID f=(*e)->GetFieldID(e,c,"value","Ljava/lang/String;");
        if(f)result.l=(*e)->GetObjectField(e,receiver,f);
        (*e)->DeleteLocalRef(e,c);break;
    }
    case PREPARE:{
        const char *source=(*e)->GetStringUTFChars(e,a[0].l,NULL);if(!source)break;
        const char *dest=(*e)->GetStringUTFChars(e,a[1].l,NULL);
        if(dest){
            if(s->ops.prepare_file(s->ops.ctx,source,dest)&&s->ops.log)s->ops.log(s->ops.ctx,"MSDK asset copy failed");
            (*e)->ReleaseStringUTFChars(e,a[1].l,(char *)dest);
        }
        (*e)->ReleaseStringUTFChars(e,a[0].l,(char *)source);break;
    }
    case LOG:{
        const char *message=(*e)->GetStringUTFChars(e,a[1].l,NULL);
        if(message){if(s->ops.log)s->ops.log(s->ops.ctx,message);(*e)->ReleaseStringUTFChars(e,a[1].l,(char *)message);}break;
    }
    case CRYPTO_UNAVAILABLE:error(e,"java/lang/UnsupportedOperationException","Online authentication is unavailable on Vita");break;
    }
    if(text)result.l=(*e)->NewStringUTF(e,text);
    return result;
}
int acp_sdk_configure(acp_sdk *s,acp_platform *p,acp_jni_registry *registry,const acp_sdk_ops *ops){
    if(!s||!p||!registry||!ops||!ops->random||!ops->prepare_file||!valid_uuid(ops->installation_id))return -1;
    *s=(acp_sdk){.platform=p,.registry=registry,.ops=*ops};strcpy(s->id,ops->installation_id);s->ops.installation_id=s->id;sdk=s;
    acp_jni_runtime *r=p->runtime;
    const char *classes[]={UTILS,AUTH,UDID,FLURRY,"ubisoft/mobile/mobileSDK/Msdk","java/util/UUID","java/util/TimeZone","java/security/Key","javax/crypto/Mac"};
    for(unsigned i=0;i<sizeof(classes)/sizeof(*classes);i++)if(acp_jni_runtime_class(r,classes[i],"java/lang/Object"))return -1;
    if(acp_jni_runtime_class(r,"javax/crypto/spec/SecretKeySpec","java/security/Key")||
       acp_jni_runtime_exception_class(r,"java/lang/UnsupportedOperationException","java/lang/RuntimeException"))return -1;
    for(unsigned i=0;i<sizeof(methods)/sizeof(*methods);i++){
        const method *m=methods+i;acp_jni_method_def d={m->cls,m->name,m->sig,m->is_static,call,(void *)m};
        if(acp_jni_runtime_methods(r,&d,1))return -1;
    }
    const acp_jni_field_def fields[]={{AUTH,"_ctx","Landroid/content/Context;",1},{"java/util/UUID","value","Ljava/lang/String;",0},
        {UTILS,"s_activity","Landroid/app/Activity;",1}};
    if(acp_jni_runtime_fields(r,fields,3))return -1;
    const char *natives[][2]={
        {"MsdkJniOnActivityResult","(IILandroid/content/Intent;)V"},{"MsdkJniOnCreate","(Landroid/os/Bundle;)V"},
        {"MsdkJniOnDestroy","()V"},{"MsdkJniOnNewIntent","(Landroid/content/Intent;)V"},
        {"MsdkJniOnPause","()V"},{"MsdkJniOnRestart","()V"},{"MsdkJniOnResume","()V"},
        {"MsdkJniOnSaveInstanceState","(Landroid/os/Bundle;)V"},{"MsdkJniOnStop","()V"}};
    for(unsigned i=0;i<sizeof(natives)/sizeof(*natives);i++)
        if(acp_jni_declare(registry,"ubisoft/mobile/mobileSDK/Msdk",natives[i][0],natives[i][1]))return -1;
    return 0;
}
