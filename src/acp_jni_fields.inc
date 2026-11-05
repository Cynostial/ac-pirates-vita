/* Sparse instance fields: no per-object unbounded map. Static fields live in
 * immutable definitions; their current reference values are class-loader roots. */
static jr_field *field_find(acp_jni_runtime *r,int cls,const char *name,const char *sig) {
    for(int c=cls;c>=0;c=r->types[c].parent)for(unsigned i=0;i<r->fields;i++) {
        jr_field *f=&r->field[i];if(f->cls==(unsigned)c&&!strcmp(f->name,name)&&!strcmp(f->signature,sig))return f;
    }
    return NULL;
}
static int field_definition(acp_jni_runtime *r,const acp_jni_field_def *d,jr_field *out) {
    int cls=class_index(r,d->class_name);
    if(cls<0||!d->name||!*d->name||strlen(d->name)>=ACP_JNI_METHOD_NAME||!d->signature||
       !*d->signature||strlen(d->signature)>=ACP_JNI_SIGNATURE-2||d->signature[0]=='V'||(d->is_static!=0&&d->is_static!=1))return 0;
    for(const char *p=d->name;*p;p++)if((unsigned char)*p<=32||strchr(".;[/<>()",*p))return 0;
    char signature[ACP_JNI_SIGNATURE];signature[0]='(';signature[1]=')';strcpy(signature+2,d->signature);
    if(!acp_jni_valid_signature(signature))return 0;
    memset(out,0,sizeof(*out));strcpy(out->name,d->name);strcpy(out->signature,d->signature);out->cls=(unsigned)cls;out->is_static=(unsigned)d->is_static;return 1;
}
int acp_jni_runtime_fields(acp_jni_runtime *r,const acp_jni_field_def *defs,unsigned count) {
    if(!r||(count&&!defs)||count>JR_FIELDS)return JNI_EINVAL;
    r->ops.lock(r->ops.ctx);int result=JNI_ERR;jr_field temp;
    if(r->sealed||count>JR_FIELDS-r->fields)goto done;
    for(unsigned i=0;i<count;i++) {
        if(!field_definition(r,&defs[i],&temp))goto done;
        jr_field *prior=field_find(r,(int)temp.cls,temp.name,temp.signature);
        if(prior&&prior->cls==temp.cls)goto done;
        for(unsigned j=0;j<i;j++)if(!strcmp(defs[i].class_name,defs[j].class_name)&&!strcmp(defs[i].name,defs[j].name)&&!strcmp(defs[i].signature,defs[j].signature))goto done;
    }
    for(unsigned i=0;i<count;i++){(void)field_definition(r,&defs[i],&r->field[r->fields]);r->fields++;}result=JNI_OK;
done:
    r->ops.unlock(r->ops.ctx);return result;
}
static jr_field *field_id(jr_thread *t,jfieldID id) {
    uintptr_t address=(uintptr_t)id,base=(uintptr_t)t->r->field;
    if(address<base||address-base>=t->r->fields*sizeof(jr_field)||(address-base)%sizeof(jr_field)){exception(t,C_ARGUMENT);return NULL;}
    return &t->r->field[(address-base)/sizeof(jr_field)];
}
static jfieldID get_field_id(JNIEnv *env,jclass cls,const char *name,const char *sig,unsigned is_static) {
    jr_thread *t=enter(env);jfieldID result=NULL;
    if(!t->pending) {
        int c=class_from_object(t,cls);
        if(!name||!sig)exception(t,C_NULL);
        else if(c>=0){jr_field *f=field_find(t->r,c,name,sig);
            if(!f||f->is_static!=is_static)named_exception(t,C_NOFIELD,t->r->types[c].name,name,sig);
            else result=(jfieldID)f;}
    }
    leave(t);return result;
}
static jfieldID j_GetFieldID(JNIEnv *env,jclass cls,const char *name,const char *sig){return get_field_id(env,cls,name,sig,0);}
static jfieldID j_GetStaticFieldID(JNIEnv *env,jclass cls,const char *name,const char *sig){return get_field_id(env,cls,name,sig,1);}
static jvalue field_access(JNIEnv *env,jobject receiver,jfieldID id,unsigned is_static,char kind,int write,jvalue input) {
    jr_thread *t=enter(env);jvalue result={0};
    if(t->pending){leave(t);return result;}
    jr_field *f=field_id(t,id);if(!f)goto done;
    if(f->is_static!=is_static||descriptor_kind(f->signature)!=kind){exception(t,C_ARGUMENT);goto done;}
    unsigned owner=0;int actual;
    if(is_static)actual=class_from_object(t,(jclass)receiver);
    else{jr_object *o=object_get(t,receiver,0);actual=o?(int)o->cls:-1;owner=ref_object(t,receiver);}
    if(actual<0)goto done;
    if(!assignable(t->r,(unsigned)actual,f->cls)){exception(t,C_ARGUMENT);goto done;}
    if(write&&kind=='L'&&!reference_matches(t,input.l,f->signature))goto done;
    jvalue *value=&f->value;jr_field_cell *cell=NULL;
    if(!is_static) {
        unsigned index=(unsigned)(f-t->r->field);jr_field_cell *empty=NULL;
        for(unsigned i=0;i<JR_FIELD_CELLS;i++) {
            jr_field_cell *candidate=&t->r->cells[i];
            if(candidate->object==owner&&candidate->field==index){cell=candidate;break;}
            if(!candidate->object&&!empty)empty=candidate;
        }
        if(!cell&&write) {
            if(!empty){exception(t,C_OOM);goto done;}
            cell=empty;*cell=(jr_field_cell){.object=owner,.field=index};t->r->field_cells++;
        }
        if(!cell)goto done;
        value=&cell->value;
    }
    if(write) {
        if(kind=='L') {
            unsigned previous=(unsigned)(uintptr_t)value->l,new_id=ref_object(t,input.l);
            int cycles=previous!=new_id&&object_has_links(t->r,previous);
            if(new_id)t->r->objects[new_id-1].refs++;
            if(owner){
                jr_object *o=&t->r->objects[owner-1];
                if(previous)o->reference_fields--;
                if(new_id)o->reference_fields++;
            }
            value->l=(jobject)(uintptr_t)new_id;object_drop(t->r,previous);
            if(cycles)collect_array_cycles(t->r);
        } else *value=input;
    } else if(kind=='L')result.l=ref_new(t,(unsigned)(uintptr_t)value->l,JNILocalRefType);
    else result=*value;
done:
    leave(t);return result;
}
#define FIELD(Name,Type,Member,Code) \
static Type j_Get##Name##Field(JNIEnv *e,jobject o,jfieldID f){jvalue v=field_access(e,o,f,0,Code,0,(jvalue){0});return v.Member;} \
static Type j_GetStatic##Name##Field(JNIEnv *e,jclass c,jfieldID f){jvalue v=field_access(e,c,f,1,Code,0,(jvalue){0});return v.Member;} \
static void j_Set##Name##Field(JNIEnv *e,jobject o,jfieldID f,Type value){field_access(e,o,f,0,Code,1,(jvalue){.Member=value});} \
static void j_SetStatic##Name##Field(JNIEnv *e,jclass c,jfieldID f,Type value){field_access(e,c,f,1,Code,1,(jvalue){.Member=value});}
FIELD(Object,jobject,l,'L') FIELD(Boolean,jboolean,z,'Z') FIELD(Byte,jbyte,b,'B') FIELD(Char,jchar,c,'C')
FIELD(Short,jshort,s,'S') FIELD(Int,jint,i,'I') FIELD(Long,jlong,j,'J') FIELD(Float,jfloat,f,'F') FIELD(Double,jdouble,d,'D')
#undef FIELD
