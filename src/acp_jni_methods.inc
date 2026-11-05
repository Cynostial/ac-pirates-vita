/* Private implementation included by acp_jni_runtime.c. No allocation on calls. */
static const char *descriptor_end(const char *s) {
    while (*s=='[') s++;
    if (*s++=='L') { while (*s!=';') s++; s++; }
    return s;
}
static char descriptor_kind(const char *s) { return *s=='[' ? 'L' : *s; }
static jr_method *method_find(acp_jni_runtime *r, int cls, const char *name, const char *sig) {
    for (int c=cls; c>=0; c=r->types[c].parent) {
        for (unsigned i=0; i<r->methods; i++) {
            jr_method *m=&r->method[i];
            if (m->cls==(unsigned)c && !strcmp(m->name,name) && !strcmp(m->signature,sig)) return m;
        }
        if (!strcmp(name,"<init>")) break; /* constructors are not inherited */
    }
    return NULL;
}
static jr_method *method_id(jr_thread *t, jmethodID id) {
    uintptr_t address=(uintptr_t)id, base=(uintptr_t)t->r->method;
    if (address<base || address-base>=t->r->methods*sizeof(jr_method) ||
        (address-base)%sizeof(jr_method)) { exception(t,C_ARGUMENT); return NULL; }
    return &t->r->method[(address-base)/sizeof(jr_method)];
}
static int method_definition(acp_jni_runtime *r, const acp_jni_method_def *d, jr_method *m) {
    int c=class_index(r,d->class_name);
    if (c<0 || !d->name || !*d->name || strlen(d->name)>=ACP_JNI_METHOD_NAME ||
        !acp_jni_valid_signature(d->signature) || !d->call || (d->is_static!=0 && d->is_static!=1)) return 0;
    int ctor=!strcmp(d->name,"<init>");
    if (!ctor) for (const char *p=d->name; *p; p++)
        if ((unsigned char)*p<=32 || strchr(".;[/<>()",*p)) return 0;
    memset(m,0,sizeof(*m));
    const char *s=d->signature+1;
    while (*s!=')') {
        if (m->argc==JR_ARGS) return 0;
        m->arg_offset[m->argc++]=(unsigned short)(s-d->signature);
        s=descriptor_end(s);
    }
    m->result_offset=(unsigned)(s+1-d->signature);
    if (ctor && (d->is_static || s[1]!='V')) return 0;
    strcpy(m->name,d->name); strcpy(m->signature,d->signature);
    m->cls=(unsigned)c; m->is_static=(unsigned)d->is_static; m->call=d->call; m->ctx=d->ctx;
    return 1;
}
int acp_jni_runtime_methods(acp_jni_runtime *r, const acp_jni_method_def *defs, unsigned n) {
    if (!r || (n && !defs) || n>JR_METHODS) return JNI_EINVAL;
    r->ops.lock(r->ops.ctx); int status=JNI_ERR; jr_method temp;
    if (r->sealed || n>JR_METHODS-r->methods) goto done;
    for (unsigned i=0; i<n; i++) {
        if (!method_definition(r,&defs[i],&temp)) goto done;
        jr_method *prior=method_find(r,(int)temp.cls,temp.name,temp.signature);
        if (prior && (prior->cls==temp.cls || prior->is_static!=temp.is_static)) goto done;
        for (unsigned j=0; j<i; j++) if (!strcmp(defs[j].name,defs[i].name) &&
                                       !strcmp(defs[j].signature,defs[i].signature)) {
            int other=class_index(r,defs[j].class_name);
            if (other==(int)temp.cls || (strcmp(temp.name,"<init>") &&
                defs[j].is_static!=defs[i].is_static &&
                (assignable(r,temp.cls,(unsigned)other) || assignable(r,(unsigned)other,temp.cls)))) goto done;
        }
    }
    for (unsigned i=0; i<n; i++) {
        (void)method_definition(r,&defs[i],&r->method[r->methods]); r->methods++;
    }
    status=JNI_OK;
done:
    r->ops.unlock(r->ops.ctx); return status;
}
static jmethodID get_method(JNIEnv *env, jclass cls, const char *name, const char *sig, int is_static) {
    jr_thread *t=enter(env); jmethodID out=NULL;
    if (!t->pending) {
        int c=class_from_object(t,cls);
        if (!name || !sig) exception(t,C_NULL);
        else if (c>=0) {
            jr_method *m=method_find(t->r,c,name,sig);
            if (!m || m->is_static!=(unsigned)is_static)
                named_exception(t,C_NOMETHOD,t->r->types[c].name,name,sig);
            else out=(jmethodID)m;
        }
    }
    leave(t); return out;
}
static jmethodID j_GetMethodID(JNIEnv *e,jclass c,const char *n,const char *s) { return get_method(e,c,n,s,0); }
static jmethodID j_GetStaticMethodID(JNIEnv *e,jclass c,const char *n,const char *s) { return get_method(e,c,n,s,1); }
/* Check class identity without casting a reference into native object memory. */
static int reference_matches(jr_thread *t, jobject ref, const char *descriptor) {
    if (ref_is_null(t,ref)) return 1;
    jr_object *o=object_get(t,ref,0); if (!o) return 0;
    char name[ACP_JNI_CLASS_NAME];
    const char *start=descriptor+(*descriptor=='L');
    size_t len=(size_t)(descriptor_end(descriptor)-start)-(*descriptor=='L');
    if (len>=sizeof(name)) { exception(t,C_NOCLASS); return 0; }
    memcpy(name,start,len); name[len]=0;
    int c=resolve_class(t->r,name);
    if (c<0) { exception(t,C_NOCLASS); return 0; }
    if (!assignable(t->r,o->cls,(unsigned)c)) { exception(t,C_ARGUMENT); return 0; }
    return 1;
}
/* mode: 0 virtual, 1 nonvirtual, 2 static. A callback frame roots arguments and
 * isolates temporary JNI references. The mutex is released during host work. */
static jvalue invoke_method(JNIEnv *env, jobject object, jclass cls, jmethodID id,
                            const jvalue *args, va_list *varargs, unsigned mode, char result_kind) {
    jvalue result={0}, values[JR_ARGS]; jr_thread *t=enter(env);
    if (t->pending) { leave(t); return result; }
    jr_method *m=method_id(t,id);
    if (!m) { leave(t); return result; }
    if (m->is_static!=(mode==2) || descriptor_kind(m->signature+m->result_offset)!=result_kind) {
        exception(t,C_ARGUMENT); leave(t); return result;
    }
    int c;
    if (mode==2) c=class_from_object(t,cls);
    else {
        jr_object *o=object_get(t,object,0); c=o ? (int)o->cls : -1;
        if (c>=0 && mode==1) {
            int explicit_class=class_from_object(t,cls);
            if (explicit_class<0 || !assignable(t->r,(unsigned)c,(unsigned)explicit_class)) {
                exception(t,C_ARGUMENT); c=-1;
            } else c=explicit_class;
        }
    }
    if (c<0) { leave(t); return result; }
    if (!assignable(t->r,(unsigned)c,m->cls) ||
        (!strcmp(m->name,"<init>") && (unsigned)c!=m->cls)) {
        exception(t,C_ARGUMENT); leave(t); return result;
    }
    if (mode!=2) m=method_find(t->r,c,m->name,m->signature);
    if (!m) { exception(t,C_NOMETHOD); leave(t); return result; }
    if ((!args && !varargs && m->argc) || t->depth+1==JR_FRAMES || t->calls==JR_FRAMES) {
        exception(t,(!args && !varargs && m->argc) ? C_NULL : C_OOM); leave(t); return result;
    }
    for (unsigned i=0; i<m->argc; i++) {
        const char *desc=m->signature+m->arg_offset[i];
        char kind=descriptor_kind(desc);
        if (!varargs) values[i]=args[i];
        else switch (kind) {
            case 'Z': values[i].z=(jboolean)va_arg(*varargs,int); break;
            case 'B': values[i].b=(jbyte)va_arg(*varargs,int); break;
            case 'C': values[i].c=(jchar)va_arg(*varargs,int); break;
            case 'S': values[i].s=(jshort)va_arg(*varargs,int); break;
            case 'I': values[i].i=va_arg(*varargs,jint); break;
            case 'J': values[i].j=va_arg(*varargs,jlong); break;
            case 'F': values[i].f=(jfloat)va_arg(*varargs,double); break;
            case 'D': values[i].d=va_arg(*varargs,jdouble); break;
            case 'L': values[i].l=va_arg(*varargs,jobject); break;
            default: fail(t->r,"Invalid registered JNI descriptor");
        }
        if (kind=='L' && !reference_matches(t,values[i].l,desc)) { leave(t); return result; }
    }
    t->frames[++t->depth]=(jr_frame){0};
    if (capacity(t,(jint)m->argc+17)!=JNI_OK) { t->depth--; leave(t); return result; }
    unsigned frame=t->depth;
    jobject receiver=ref_new(t,ref_object(t,mode==2 ? cls : object),JNILocalRefType);
    for (unsigned i=0; i<m->argc; i++) if (descriptor_kind(m->signature+m->arg_offset[i])=='L')
        values[i].l=ref_new(t,ref_object(t,values[i].l),JNILocalRefType);
    t->call_frames[t->calls++]=frame;
    leave(t);
    result=m->call(env,receiver,values,m->argc,m->ctx);
    t=enter(env);
    if (t->depth!=frame) exception(t,C_STATE);
    unsigned object_result=0;
    if (!t->pending && result_kind=='L' && reference_matches(t,result.l,m->signature+m->result_offset)) {
        object_result=ref_object(t,result.l);
        if (object_result) t->r->objects[object_result-1].refs++;
    }
    while (t->depth>=frame) { clear_frame(t); t->depth--; }
    t->calls--;
    if (t->pending) result=(jvalue){0};
    else if (result_kind=='L') result.l=ref_new(t,object_result,JNILocalRefType);
    object_drop(t->r,object_result);
    leave(t); return result;
}
static jobject j_AllocObject(JNIEnv *env, jclass cls) {
    jr_thread *t=enter(env); jobject out=NULL; int c=class_from_object(t,cls);
    if (!t->pending && c>=0) {
        if (c==C_CLASS || c==C_STRING || t->r->types[c].name[0]=='[' || assignable(t->r,(unsigned)c,C_BUFFER)) exception(t,C_ARGUMENT);
        else {
            unsigned id=object_new(t->r,K_OBJECT,(unsigned)c);
            if (!id) exception(t,C_OOM);
            else { out=ref_new(t,id,JNILocalRefType); if (!out) object_collect(t->r,id); }
        }
    }
    leave(t); return out;
}
static jobject new_object(JNIEnv *env,jclass cls,jmethodID id,const jvalue *args,va_list *v) {
    jr_thread *t=enter(env); jr_method *m=method_id(t,id); int c=class_from_object(t,cls);
    int valid=!t->pending && m && c>=0 && m->cls==(unsigned)c && !m->is_static && !strcmp(m->name,"<init>");
    if (!valid) exception(t,C_ARGUMENT);
    leave(t);
    if (!valid) return NULL;
    jobject out=j_AllocObject(env,cls);
    if (out) {
        (void)invoke_method(env,out,cls,id,args,v,1,'V');
        if (j_ExceptionCheck(env)) { j_DeleteLocalRef(env,out); out=NULL; }
    }
    return out;
}
static jobject j_NewObjectA(JNIEnv *e,jclass c,jmethodID m,const jvalue *a) { return new_object(e,c,m,a,NULL); }
static jobject j_NewObjectV(JNIEnv *e,jclass c,jmethodID m,va_list a) {
    va_list copy; va_copy(copy,a); jobject out=new_object(e,c,m,NULL,&copy); va_end(copy); return out;
}
static jobject j_NewObject(JNIEnv *e,jclass c,jmethodID m,...) {
    va_list a; va_start(a,m); jobject out=new_object(e,c,m,NULL,&a); va_end(a); return out;
}
