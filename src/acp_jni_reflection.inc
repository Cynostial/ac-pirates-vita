static jobject reflected_method(jr_thread *t,jr_method *m) {
    const char *name=!strcmp(m->name,"<init>")?"java/lang/reflect/Constructor":"java/lang/reflect/Method";
    int cls=class_index(t->r,name);unsigned id=object_new(t->r,K_REFLECT_METHOD,(unsigned)cls);
    if(!id){exception(t,C_OOM);return NULL;}
    t->r->objects[id-1].length=(jsize)(m-t->r->method);return string_ref(t,id);
}
static jobject reflected_field(jr_thread *t,jr_field *f) {
    int cls=class_index(t->r,"java/lang/reflect/Field");unsigned id=object_new(t->r,K_REFLECT_FIELD,(unsigned)cls);
    if(!id){exception(t,C_OOM);return NULL;}
    t->r->objects[id-1].length=(jsize)(f-t->r->field);return string_ref(t,id);
}
static jobject j_ToReflectedMethod(JNIEnv *env,jclass cls,jmethodID id,jboolean is_static) {
    jr_thread *t=enter(env);jobject result=NULL;jr_method *m=method_id(t,id);int c=class_from_object(t,cls);
    if(m&&c>=0) {
        if(m->is_static!=!!is_static||!assignable(t->r,(unsigned)c,m->cls))exception(t,C_ARGUMENT);
        else result=reflected_method(t,m);
    }
    leave(t);return result;
}
static jobject j_ToReflectedField(JNIEnv *env,jclass cls,jfieldID id,jboolean is_static) {
    jr_thread *t=enter(env);jobject result=NULL;jr_field *f=field_id(t,id);int c=class_from_object(t,cls);
    if(f&&c>=0) {
        if(f->is_static!=!!is_static||!assignable(t->r,(unsigned)c,f->cls))exception(t,C_ARGUMENT);
        else result=reflected_field(t,f);
    }
    leave(t);return result;
}
static jmethodID j_FromReflectedMethod(JNIEnv *env,jobject obj) {
    jr_thread *t=enter(env);jr_object *o=object_get(t,obj,K_REFLECT_METHOD);jmethodID result=NULL;
    if(o){if(o->length<0||(unsigned)o->length>=t->r->methods)exception(t,C_ARGUMENT);else result=(jmethodID)&t->r->method[o->length];}
    leave(t);return result;
}
static jfieldID j_FromReflectedField(JNIEnv *env,jobject obj) {
    jr_thread *t=enter(env);jr_object *o=object_get(t,obj,K_REFLECT_FIELD);jfieldID result=NULL;
    if(o){if(o->length<0||(unsigned)o->length>=t->r->fields)exception(t,C_ARGUMENT);else result=(jfieldID)&t->r->field[o->length];}
    leave(t);return result;
}
