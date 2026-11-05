/* Primitive arrays use the same bounded 256 KiB payload pool as strings.
 * All elements start at a 256-byte boundary (including long/double). Direct
 * element pointers are pinned per thread, so no separate copy can evade the
 * budget. A zero-length array owns one block and has a releasable pointer. */
static jarray primitive_array_new(JNIEnv *env,jsize count,char type,size_t stride) {
    jr_thread *t=enter(env);jarray result=NULL;
    if(count<0)exception(t,C_NEGATIVE_ARRAY);
    else if((uint32_t)count>sizeof(t->r->payload)/stride)exception(t,C_OOM);
    else {
        char descriptor[3]={'[',type,0};int cls=class_index(t->r,descriptor);
        if(cls<0)exception(t,C_NOCLASS);
        else {
            unsigned id=object_new(t->r,K_PRIMITIVE_ARRAY,(unsigned)cls);
            if(!id)exception(t,C_OOM);
            else {
                jr_object *o=&t->r->objects[id-1];size_t bytes=(size_t)count*stride;
                o->length=count;o->utf_length=type;o->capacity=(jlong)stride;
                o->data=payload_new(t,id,bytes?bytes:1);
                if(o->data){memset(o->data,0,bytes);result=ref_new(t,id,JNILocalRefType);}
                if(!result)object_collect(t->r,id);
            }
        }
    }
    leave(t);return result;
}
static jsize j_GetArrayLength(JNIEnv *env,jarray array) {
    jr_thread *t=enter(env);jr_object *o=object_get(t,array,0);jsize length=0;
    if(o){if(o->kind==K_PRIMITIVE_ARRAY||o->kind==K_OBJECT_ARRAY)length=o->length;else exception(t,C_ARGUMENT);}
    leave(t);return length;
}
static jr_object *primitive_array_get(jr_thread *t,jarray array,char type) {
    jr_object *o=object_get(t,array,K_PRIMITIVE_ARRAY);
    if(o&&type&&o->utf_length!=type){exception(t,C_ARGUMENT);return NULL;}return o;
}
static void *primitive_array_pin(JNIEnv *env,jarray array,jboolean *copy,char type,unsigned critical) {
    jr_thread *t=enter(env);jr_object *o=primitive_array_get(t,array,type);void *result=NULL;
    if(o) {
        unsigned id=ref_object(t,array);jr_pin *pin=NULL,*empty=NULL;
        for(unsigned i=0;i<JR_PINS;i++) {
            jr_pin *p=&t->r->pins[i];
            if(!p->count&&!empty)empty=p;
            if(p->count&&p->object==id&&p->owner==t->index&&p->utf==2+critical)pin=p;
        }
        if(!pin)pin=empty;
        if(!pin||pin->count==UINT_MAX||o->pins==UINT_MAX||t->pins==UINT_MAX)exception(t,C_OOM);
        else {
            if(!pin->count)*pin=(jr_pin){.object=id,.owner=t->index,.utf=2+critical};
            pin->count++;o->pins++;t->pins++;result=o->data;if(copy)*copy=JNI_FALSE;
        }
    }
    leave(t);return result;
}
static void primitive_array_release(JNIEnv *env,jarray array,void *ptr,jint mode,char type,unsigned critical) {
    jr_thread *t=enter(env);jr_object *o=primitive_array_get(t,array,type);
    if(o) {
        unsigned id=ref_object(t,array);jr_pin *pin=NULL;
        for(unsigned i=0;i<JR_PINS;i++) {
            jr_pin *p=&t->r->pins[i];
            if(p->count&&p->object==id&&p->owner==t->index&&p->utf==2+critical)pin=p;
        }
        if(ptr!=o->data||!pin||(mode!=0&&mode!=JNI_COMMIT&&mode!=JNI_ABORT))exception(t,C_ARGUMENT);
        else {
            /* JNI mode has no effect when isCopy is false. Writes already
             * address the backing array, including when JNI_ABORT is passed. */
            pin->count--;o->pins--;t->pins--;object_collect(t->r,id);
        }
    }
    leave(t);
}
static void primitive_array_region(JNIEnv *env,jarray array,jsize start,jsize count,void *data,char type,int write) {
    jr_thread *t=enter(env);jr_object *o=primitive_array_get(t,array,type);
    if(o) {
        if(start<0||count<0||start>o->length||count>o->length-start)exception(t,C_ARRAY_BOUNDS);
        else if(count&&!data)exception(t,C_NULL);
        else if(count) {
            size_t stride=(size_t)o->capacity;void *part=(unsigned char*)o->data+(size_t)start*stride;
            if(write)memmove(part,data,(size_t)count*stride);else memmove(data,part,(size_t)count*stride);
        }
    }
    leave(t);
}
#define ARRAY(Name,Type,Code) \
static Type##Array j_New##Name##Array(JNIEnv *e,jsize n){return primitive_array_new(e,n,Code,sizeof(Type));} \
static Type *j_Get##Name##ArrayElements(JNIEnv *e,Type##Array a,jboolean *copy){return primitive_array_pin(e,a,copy,Code,0);} \
static void j_Release##Name##ArrayElements(JNIEnv *e,Type##Array a,Type *p,jint mode){primitive_array_release(e,a,p,mode,Code,0);} \
static void j_Get##Name##ArrayRegion(JNIEnv *e,Type##Array a,jsize start,jsize count,Type *p){primitive_array_region(e,a,start,count,p,Code,0);} \
static void j_Set##Name##ArrayRegion(JNIEnv *e,Type##Array a,jsize start,jsize count,const Type *p){primitive_array_region(e,a,start,count,(void*)p,Code,1);}
ARRAY(Boolean,jboolean,'Z') ARRAY(Byte,jbyte,'B') ARRAY(Char,jchar,'C') ARRAY(Short,jshort,'S')
ARRAY(Int,jint,'I') ARRAY(Long,jlong,'J') ARRAY(Float,jfloat,'F') ARRAY(Double,jdouble,'D')
#undef ARRAY
static void *j_GetPrimitiveArrayCritical(JNIEnv *env,jarray array,jboolean *copy){return primitive_array_pin(env,array,copy,0,1);}
static void j_ReleasePrimitiveArrayCritical(JNIEnv *env,jarray array,void *ptr,jint mode){primitive_array_release(env,array,ptr,mode,0,1);}
static jobjectArray j_NewObjectArray(JNIEnv *env,jsize count,jclass component,jobject initial) {
    jr_thread *t=enter(env);jobjectArray result=NULL;int cls=class_from_object(t,component);
    unsigned initial_id=ref_object(t,initial);
    if(count<0)exception(t,C_NEGATIVE_ARRAY);
    else if((unsigned)count>sizeof(t->r->payload)/sizeof(uint32_t))exception(t,C_OOM);
    else if(initial&&!ref_valid(t,initial))exception(t,C_ARGUMENT);
    else if(cls>=0) {
        if(initial_id&&!assignable(t->r,t->r->objects[initial_id-1].cls,(unsigned)cls))exception(t,C_ARRAY_STORE);
        else {
            char name[ACP_JNI_CLASS_NAME];const char *element=t->r->types[cls].name;size_t n=strlen(element);
            if(n+(element[0]=='['?2:4)>sizeof(name))exception(t,C_OOM);
            else {
                if(element[0]=='['){name[0]='[';memcpy(name+1,element,n+1);}
                else{name[0]='[';name[1]='L';memcpy(name+2,element,n);name[n+2]=';';name[n+3]=0;}
                int array_cls=class_index(t->r,name);
                if(array_cls<0&&add_class(t->r,name,C_OBJECT)==JNI_OK)array_cls=class_index(t->r,name);
                if(array_cls<0)exception(t,C_OOM);
                else {
                    unsigned id=object_new(t->r,K_OBJECT_ARRAY,(unsigned)array_cls);
                    if(!id)exception(t,C_OOM);
                    else {
                        t->r->object_arrays++;jr_object *o=&t->r->objects[id-1];o->utf_length=cls;
                        o->data=payload_new(t,id,count?(size_t)count*sizeof(uint32_t):1);
                        if(o->data) {
                            o->length=count;uint32_t *items=o->data;
                            for(jsize i=0;i<count;i++){items[i]=initial_id;if(initial_id)t->r->objects[initial_id-1].refs++;}
                            result=ref_new(t,id,JNILocalRefType);
                        }
                        if(!result)object_collect(t->r,id);
                    }
                }
            }
        }
    }
    leave(t);return result;
}
jobjectArray acp_jni_runtime_array_like(JNIEnv *env,jobjectArray prototype,jsize count) {
    jr_thread *t=enter(env);jr_object *o=object_get(t,prototype,K_OBJECT_ARRAY);
    jclass component=o?ref_new(t,t->r->types[o->utf_length].object,JNILocalRefType):NULL;
    leave(t);if(!component)return NULL;
    jobjectArray result=j_NewObjectArray(env,count,component,NULL);
    j_DeleteLocalRef(env,component);return result;
}
static jobject j_GetObjectArrayElement(JNIEnv *env,jobjectArray array,jsize index) {
    jr_thread *t=enter(env);jr_object *o=object_get(t,array,K_OBJECT_ARRAY);jobject result=NULL;
    if(o){if(index<0||index>=o->length)exception(t,C_ARRAY_BOUNDS);else result=ref_new(t,((uint32_t*)o->data)[index],JNILocalRefType);}
    leave(t);return result;
}
static void j_SetObjectArrayElement(JNIEnv *env,jobjectArray array,jsize index,jobject value) {
    jr_thread *t=enter(env);jr_object *o=object_get(t,array,K_OBJECT_ARRAY);unsigned id=ref_object(t,value);
    if(o) {
        if(index<0||index>=o->length)exception(t,C_ARRAY_BOUNDS);
        else if(value&&!ref_valid(t,value))exception(t,C_ARGUMENT);
        else if(id&&!assignable(t->r,t->r->objects[id-1].cls,(unsigned)o->utf_length))exception(t,C_ARRAY_STORE);
        else {
            uint32_t *items=o->data;unsigned previous=items[index];items[index]=id;
            int cycles=previous!=id&&object_has_links(t->r,previous);
            if(id)t->r->objects[id-1].refs++;
            object_drop(t->r,previous);if(cycles)collect_array_cycles(t->r);
        }
    }
    leave(t);
}
