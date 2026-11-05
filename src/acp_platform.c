#include "acp_platform.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Size of the installed OBB (original or pre-decrypted), set at launch. */
unsigned acp_obb_bytes=817249909u;
enum { DATA_FOLDER,EXTERNAL_FOLDER,PACKAGE_NAME,CODE_PATH,OBB_PATHS,OBB_SIZE,
       SCREEN_SIZE,FILESYSTEM_MODE,ROTATION,OBB_READY,ARGS_MAP,MAP_KEYS,MAP_ENTRIES,
       MAP_GET,MAP_SIZE,ITERATOR,ITERATOR_HAS,ITERATOR_NEXT,ENTRY_KEY,ENTRY_VALUE,
       SPLASH,NOTIFY,INIT_OBB,PERMISSION,CLASS_LOADER,ASSETS,LOAD_CLASS,DOWNLOAD_OBB,SET_SIZE,HIDE_SPLASH,
       RESOURCES,CONFIGURATION,WINDOW_MANAGER,DEFAULT_DISPLAY,DISPLAY_ROTATION,
       FILES_DIR,CACHE_DIR,FILE_PATH,APP_CONTEXT,COPY_CACHE };
static void throw(JNIEnv *e,const char *name,const char *message){
    jclass c=(*e)->FindClass(e,name);
    if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}
}
static jvalue call(JNIEnv *e,jobject receiver,const jvalue *args,unsigned argc,void *ctx){
    (void)argc;
    unsigned op=(unsigned)(uintptr_t)ctx;
    jvalue r={0};
    acp_platform *p=NULL;
    if(op<=ARGS_MAP||(op>=SPLASH&&op<=ASSETS))p=acp_jni_runtime_data(e,receiver,ACP_ACTIVITY);
    else if(op<=MAP_SIZE)p=acp_jni_runtime_data(e,receiver,"java/util/HashMap");
    if((*e)->ExceptionCheck(e))return r;
    const char *s=NULL;
    switch(op){
    case COPY_CACHE:{
        p=acp_jni_runtime_data(e,receiver,ACP_ACTIVITY);if(!p)break;
        const char *source=(*e)->GetStringUTFChars(e,args[0].l,NULL);if(!source)break;
        const char *name=(*e)->GetStringUTFChars(e,args[1].l,NULL);
        if(name){
            char path[512]={0};
            /* Java returns null on an I/O failure; a missing asset is not a
             * pending JNI exception that can poison later device queries. */
            if(p->cache_file&&p->cache_file(p->cache_context,source,name,path,sizeof(path))>=0)
                r.l=(*e)->NewStringUTF(e,path);
            (*e)->ReleaseStringUTFChars(e,args[1].l,(char *)name);
        }
        (*e)->ReleaseStringUTFChars(e,args[0].l,(char *)source);break;
    }
    case DOWNLOAD_OBB:{
        p=acp_jni_runtime_data(e,receiver,ACP_ACTIVITY);
        jclass c=(*e)->GetObjectClass(e,receiver);
        jfieldID f=c?(*e)->GetFieldID(e,c,"returnCode","I"):NULL;
        if(f)(*e)->SetIntField(e,receiver,f,p&&p->obb_ready?-1:0);
        (*e)->DeleteLocalRef(e,c);
        break;
    }
    case DATA_FOLDER:s=ACP_DATA "userdata";break;
    case EXTERNAL_FOLDER:s=ACP_DATA "userdata/files";break;
    case PACKAGE_NAME:s="com.ubisoft.assassin.pirates";break;
    case CODE_PATH:s=ACP_DATA "game.apk";break;
    case OBB_PATHS:{
        if(p->status)p->status("Game requested the verified OBB archive");
        jclass cls=(*e)->FindClass(e,"java/lang/String");
        if(!cls)break;
        r.l=(*e)->NewObjectArray(e,p->obb_ready?1:0,cls,NULL);
        if(p->obb_ready&&r.l){
            jstring value=(*e)->NewStringUTF(e,ACP_DATA ACP_OBB);
            if(value){(*e)->SetObjectArrayElement(e,r.l,0,value);(*e)->DeleteLocalRef(e,value);}
        }
        (*e)->DeleteLocalRef(e,cls);break;
    }
    case OBB_SIZE:r.j=acp_obb_bytes;break;
    case SCREEN_SIZE:{
        jint size[2]={960,544};r.l=(*e)->NewIntArray(e,2);
        if(r.l)(*e)->SetIntArrayRegion(e,r.l,0,2,size);
        break;
    }
    /* Despite its name, SparkActivity.getAndroidVersion selects the asset
     * filesystem: apk=0, obb=1, sd-obb=2, sd=3. This pinned APK's resource
     * android_version is "obb". Returning SDK 19 skips zip_open/initMapDir
     * in the original UpdateSparkThread and leaves the game data unmounted.
     * Android API level remains 19 in Build.VERSION and AConfiguration. */
    case FILESYSTEM_MODE:r.i=1;break;
    case ROTATION:r.i=0;break;
    case OBB_READY:r.z=(jboolean)p->obb_ready;break;
    case ARGS_MAP:
        if(p->status)p->status("Game is reading platform arguments");
        r.l=acp_jni_runtime_object(e,"java/util/HashMap",p);break;
    case MAP_KEYS:case MAP_ENTRIES:
        r.l=acp_jni_runtime_object(e,op==MAP_KEYS?"java/util/KeySet":"java/util/EntrySet",p);break;
    case MAP_SIZE:r.i=(jint)p->pairs;break;
    case SET_SIZE:
        p=acp_jni_runtime_data(e,receiver,"java/util/Set");
        if(p)r.i=(jint)p->pairs;
        break;
    case MAP_GET:{
        if(!args[0].l)break;
        const char *key=(*e)->GetStringUTFChars(e,args[0].l,NULL);
        if(!key)break;
        for(unsigned i=0;i<p->pairs;i++)if(!strcmp(key,p->keys[i])){s=p->values[i];break;}
        (*e)->ReleaseStringUTFChars(e,args[0].l,(char *)key);break;
    }
    case ITERATOR:{
        /* Iterator owns its cursor in a JNI long field. Borrowed native data
         * points to the process-lifetime platform store, never a malloc slot. */
        p=acp_jni_runtime_data(e,receiver,"java/util/Set");
        if(!p)break;
        jclass keyset=(*e)->FindClass(e,"java/util/KeySet");
        int keys=keyset&&(*e)->IsInstanceOf(e,receiver,keyset);
        (*e)->DeleteLocalRef(e,keyset);
        r.l=acp_jni_runtime_object(e,keys?"java/util/KeyIterator":"java/util/EntryIterator",p);break;
    }
    case ITERATOR_HAS:case ITERATOR_NEXT:{
        p=acp_jni_runtime_data(e,receiver,"java/util/Iterator");if(!p)break;
        jclass cls=(*e)->GetObjectClass(e,receiver);
        jfieldID cursor=cls?(*e)->GetFieldID(e,cls,"cursor","J"):NULL;
        if(!cursor){(*e)->DeleteLocalRef(e,cls);break;}
        jlong i=(*e)->GetLongField(e,receiver,cursor);
        if(op==ITERATOR_HAS)r.z=(jboolean)(i>=0&&i<p->pairs);
        else if(i<0||i>=p->pairs)throw(e,"java/util/NoSuchElementException","iterator exhausted");
        else{
            jclass keyiter=(*e)->FindClass(e,"java/util/KeyIterator");
            if(keyiter&&(*e)->IsInstanceOf(e,receiver,keyiter))s=p->keys[i];
            else{
                r.l=acp_jni_runtime_object(e,"java/util/Map$Entry",p);
                jclass entry=(*e)->FindClass(e,"java/util/Map$Entry");
                jfieldID index=entry?(*e)->GetFieldID(e,entry,"index","J"):NULL;
                if(r.l&&index)(*e)->SetLongField(e,r.l,index,i);
                (*e)->DeleteLocalRef(e,entry);
            }
            (*e)->DeleteLocalRef(e,keyiter);
            if(!(*e)->ExceptionCheck(e))(*e)->SetLongField(e,receiver,cursor,i+1);
        }
        (*e)->DeleteLocalRef(e,cls);break;
    }
    case ENTRY_KEY:case ENTRY_VALUE:{
        p=acp_jni_runtime_data(e,receiver,"java/util/Map$Entry");if(!p)break;
        jclass cls=(*e)->GetObjectClass(e,receiver);
        jfieldID index=cls?(*e)->GetFieldID(e,cls,"index","J"):NULL;
        if(index){jlong i=(*e)->GetLongField(e,receiver,index);
            if(i>=0&&i<p->pairs)s=op==ENTRY_KEY?p->keys[i]:p->values[i];
            else throw(e,"java/lang/IllegalStateException","invalid resource entry");}
        (*e)->DeleteLocalRef(e,cls);break;
    }
    case SPLASH:case NOTIFY:break; /* No Java UI/Java waiter in this native lifecycle. */
    case HIDE_SPLASH:
        p=acp_jni_runtime_data(e,receiver,ACP_ACTIVITY);
        if(p&&p->status)p->status("Game requested splash dismissal");
        break;
    case INIT_OBB:
        if(!p->obb_ready)throw(e,"java/lang/IllegalStateException","verified OBB missing");
        break;
    case PERMISSION:r.z=JNI_TRUE;break; /* Native sandbox owns its data path. */
    case CLASS_LOADER:r.l=acp_jni_runtime_object(e,"java/lang/ClassLoader",p);break;
    case ASSETS:
        if(p->status)p->status("Game is initializing its APK asset directory");
        if(p->asset_manager)r.l=acp_jni_runtime_object(e,"android/content/res/AssetManager",p->asset_manager);
        else throw(e,"java/lang/IllegalStateException","asset manager unavailable");
        break;
    case RESOURCES:
        p=acp_jni_runtime_data(e,receiver,ACP_ACTIVITY);
        if(p)r.l=acp_jni_runtime_object(e,"android/content/res/Resources",p);
        break;
    case WINDOW_MANAGER:
        p=acp_jni_runtime_data(e,receiver,ACP_ACTIVITY);
        if(p)r.l=acp_jni_runtime_object(e,"android/view/WindowManager",p);
        break;
    case DEFAULT_DISPLAY:
        p=acp_jni_runtime_data(e,receiver,"android/view/WindowManager");
        if(p)r.l=acp_jni_runtime_object(e,"android/view/Display",p);
        break;
    case DISPLAY_ROTATION:
        /* Surface.ROTATION_0: Vita's natural display is 960x544 landscape. */
        r.i=0;break;
    case FILES_DIR:case CACHE_DIR:
        /* Process-lifetime path constants; File objects never own allocations. */
        r.l=acp_jni_runtime_object(e,"java/io/File",
            (void *)(op==FILES_DIR?ACP_DATA "userdata/files":ACP_DATA "userdata/cache"));
        break;
    case FILE_PATH:s=acp_jni_runtime_data(e,receiver,"java/io/File");break;
    case APP_CONTEXT:r.l=(*e)->NewLocalRef(e,receiver);break;
    case CONFIGURATION:{
        p=acp_jni_runtime_data(e,receiver,"android/content/res/Resources");
        if(!p)break;
        r.l=acp_jni_runtime_object(e,"android/content/res/Configuration",p);
        jclass cls=(*e)->FindClass(e,"android/content/res/Configuration");
        jfieldID orientation=cls?(*e)->GetFieldID(e,cls,"orientation","I"):NULL;
        /* Android ORIENTATION_LANDSCAPE, matching the 960x544 native window.
         * This is configuration orientation, not display rotation degrees. */
        if(r.l&&orientation)(*e)->SetIntField(e,r.l,orientation,2);
        (*e)->DeleteLocalRef(e,cls);
        break;
    }
    case LOAD_CLASS:{
        const char *value=(*e)->GetStringUTFChars(e,args[0].l,NULL);
        if(!value)break;
        char name[ACP_JNI_CLASS_NAME];size_t i=0;
        while(value[i]&&i<sizeof(name)-1){name[i]=value[i]=='.'?'/':value[i];i++;}
        int too_long=value[i]!=0;name[i]=0;
        (*e)->ReleaseStringUTFChars(e,args[0].l,(char *)value);
        if(too_long)throw(e,"java/lang/IllegalArgumentException","class name too long");
        else{
            r.l=(*e)->FindClass(e,name);
            /* ClassLoader's checked exception differs from JNI FindClass's
             * linkage error. The original MSDK helper explicitly tests it. */
            if(!r.l&&(*e)->ExceptionCheck(e)){
                jthrowable cause=(*e)->ExceptionOccurred(e);(*e)->ExceptionClear(e);
                jclass missing=(*e)->FindClass(e,"java/lang/NoClassDefFoundError");
                int is_missing=missing&&(*e)->IsInstanceOf(e,cause,missing);
                (*e)->DeleteLocalRef(e,missing);
                if(is_missing)throw(e,"java/lang/ClassNotFoundException",name);
                else (*e)->Throw(e,cause);
                (*e)->DeleteLocalRef(e,cause);
            }
        }
        break;
    }
    }
    if(s)r.l=(*e)->NewStringUTF(e,s);
    return r;
}
static uint16_t u16(const unsigned char *p){return (uint16_t)(p[0]|p[1]<<8);}
static int read_args(acp_platform *p,const char *path){
    FILE *f=fopen(path,"rb");if(!f)return -1;
    size_t size=fread(p->strings,1,sizeof(p->strings),f);
    int extra=fgetc(f),bad=ferror(f);fclose(f);
    if(bad||extra!=EOF||size<8||memcmp(p->strings,"ACPA\1\0",6))return -1;
    unsigned pairs=u16(p->strings+6);if(!pairs||pairs>256)return -1;
    size_t pos=8;
    for(unsigned i=0;i<pairs;i++){
        if(size-pos<4)return -1;
        unsigned k=u16(p->strings+pos),v=u16(p->strings+pos+2);pos+=4;
        if(!k||size-pos<k+v+2u||p->strings[pos+k]||p->strings[pos+k+1+v])return -1;
        if(memchr(p->strings+pos,0,k)||memchr(p->strings+pos+k+1,0,v))return -1;
        p->keys[i]=(char*)p->strings+pos;p->values[i]=(char*)p->strings+pos+k+1;
        for(unsigned j=0;j<i;j++)if(!strcmp(p->keys[j],p->keys[i]))return -1;
        pos+=k+v+2;
    }
    if(pos!=size)return -1;
    p->pairs=pairs;return 0;
}
int acp_platform_configure(acp_platform *p,acp_jni_runtime *r,const char *path,int obb_ready){
    if(!p||!r||!path)return -1;
    memset(p,0,sizeof(*p));p->runtime=r;p->obb_ready=!!obb_ready;
    if(read_args(p,path))return -1;
    const char *classes[][2]={
        {"android/content/Context","java/lang/Object"},{"android/app/Activity","android/content/Context"},
        {"android/app/NativeActivity","android/app/Activity"},{ACP_ACTIVITY,"android/app/NativeActivity"},
        {"org/ubisoft/geea/spark2/Permission","java/lang/Object"},{"java/lang/ClassLoader","java/lang/Object"},
        {"android/content/res/AssetManager","java/lang/Object"},
        {"android/content/res/Resources","java/lang/Object"},
        {"android/content/res/Configuration","java/lang/Object"},
        {"java/io/File","java/lang/Object"},
        {"android/view/WindowManager","java/lang/Object"},{"android/view/Display","java/lang/Object"},
        {"java/util/Map","java/lang/Object"},{"java/util/HashMap","java/util/Map"},
        {"java/util/Collection","java/lang/Object"},{"java/util/Set","java/util/Collection"},
        {"java/util/KeySet","java/util/Set"},{"java/util/EntrySet","java/util/Set"},
        {"java/util/Iterator","java/lang/Object"},{"java/util/KeyIterator","java/util/Iterator"},
        {"java/util/EntryIterator","java/util/Iterator"},{"java/util/Map$Entry","java/lang/Object"}};
    for(unsigned i=0;i<sizeof(classes)/sizeof(*classes);i++)
        if(acp_jni_runtime_class(r,classes[i][0],classes[i][1]))return -1;
    if(acp_jni_runtime_exception_class(r,"java/util/NoSuchElementException","java/lang/RuntimeException"))return -1;
    if(acp_jni_runtime_exception_class(r,"java/lang/ClassNotFoundException","java/lang/Exception"))return -1;
    acp_jni_field_def fields[]={
        {"java/util/Iterator","cursor","J",0},{"java/util/Map$Entry","index","J",0},
        {ACP_ACTIVITY,"returnCode","I",0},
        {"android/content/res/Configuration","orientation","I",0}};
    if(acp_jni_runtime_fields(r,fields,sizeof(fields)/sizeof(*fields)))return -1;
#define M(c,n,s,o) {c,n,s,0,call,(void *)(uintptr_t)o}
#define A(n,s,o) M(ACP_ACTIVITY,n,s,o)
#define C(n,s,o) M("android/content/Context",n,s,o)
    acp_jni_method_def methods[]={
        A("GetDataFolder","()Ljava/lang/String;",DATA_FOLDER),
        A("CopyFileToCacheFolder","(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",COPY_CACHE),
        A("GetExternalFilesDir","()Ljava/lang/String;",EXTERNAL_FOLDER),
        C("getPackageName","()Ljava/lang/String;",PACKAGE_NAME),
        C("getPackageCodePath","()Ljava/lang/String;",CODE_PATH),
        A("getObbPathList","()[Ljava/lang/String;",OBB_PATHS),
        A("getObbFileSize","()J",OBB_SIZE),A("GetDeviceScreenSize","()[I",SCREEN_SIZE),
        A("getAndroidVersion","()I",FILESYSTEM_MODE),A("getWindowRotation","()I",ROTATION),
        A("checkIfObbExist","()Z",OBB_READY),A("GetPlatformArgsList","()Ljava/util/HashMap;",ARGS_MAP),
        A("initObb","()V",INIT_OBB),A("showSplashScreen","()V",SPLASH),
        A("hideSplashScreen","()V",HIDE_SPLASH),A("synchronizedNotifyAll","()V",NOTIFY),
        A("hasPermission","(Ljava/lang/String;)Z",PERMISSION),
        A("waitForPermissions","()Z",PERMISSION),A("requestPermissions","()V",NOTIFY),
        A("downloadOBB","()V",DOWNLOAD_OBB),A("waitForDownloadActivityResult","()V",NOTIFY),
        /* These Android methods are inherited from Context. The engine
         * explicitly resolves getAssets through android/app/Activity. */
        C("getClassLoader","()Ljava/lang/ClassLoader;",CLASS_LOADER),
        C("getAssets","()Landroid/content/res/AssetManager;",ASSETS),
        C("getResources","()Landroid/content/res/Resources;",RESOURCES),
        C("getFilesDir","()Ljava/io/File;",FILES_DIR),
        C("getCacheDir","()Ljava/io/File;",CACHE_DIR),
        C("getExternalCacheDir","()Ljava/io/File;",CACHE_DIR),
        C("getApplicationContext","()Landroid/content/Context;",APP_CONTEXT),
        M("java/io/File","getPath","()Ljava/lang/String;",FILE_PATH),
        M("java/io/File","getAbsolutePath","()Ljava/lang/String;",FILE_PATH),
        M("android/app/Activity","getWindowManager","()Landroid/view/WindowManager;",WINDOW_MANAGER),
        M("android/view/WindowManager","getDefaultDisplay","()Landroid/view/Display;",DEFAULT_DISPLAY),
        M("android/view/Display","getRotation","()I",DISPLAY_ROTATION),
        M("android/content/res/Resources","getConfiguration","()Landroid/content/res/Configuration;",CONFIGURATION),
        M("java/lang/ClassLoader","loadClass","(Ljava/lang/String;)Ljava/lang/Class;",LOAD_CLASS),
        M("java/util/Map","keySet","()Ljava/util/Set;",MAP_KEYS),
        M("java/util/Map","entrySet","()Ljava/util/Set;",MAP_ENTRIES),
        M("java/util/Map","get","(Ljava/lang/Object;)Ljava/lang/Object;",MAP_GET),
        M("java/util/Map","size","()I",MAP_SIZE),
        M("java/util/Set","iterator","()Ljava/util/Iterator;",ITERATOR),
        M("java/util/Set","size","()I",SET_SIZE),
        M("java/util/Iterator","hasNext","()Z",ITERATOR_HAS),
        M("java/util/Iterator","next","()Ljava/lang/Object;",ITERATOR_NEXT),
        M("java/util/Map$Entry","getKey","()Ljava/lang/Object;",ENTRY_KEY),
        M("java/util/Map$Entry","getValue","()Ljava/lang/Object;",ENTRY_VALUE)};
#undef A
#undef C
#undef M
    return acp_jni_runtime_methods(r,methods,sizeof(methods)/sizeof(*methods));
}
jobject acp_platform_activity(JNIEnv *e,acp_platform *p){
    return p?acp_jni_runtime_object(e,ACP_ACTIVITY,p):NULL;
}
int acp_platform_declare_natives(acp_jni_registry *r){
    return acp_jni_declare(r,"org/ubisoft/geea/spark2/Permission",
                          "onRequestPermissionsResult","(I[Ljava/lang/String;[I)V");
}
