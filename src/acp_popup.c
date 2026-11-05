#include "acp_popup.h"
#include <string.h>
enum {SHOW,GET,RESET,CREATE,RESUME,CONSTRUCT,SCHEDULE,CANCEL,CANCEL_ALL};
enum {FREE,QUEUED,ACTIVE,DONE};
static void error(JNIEnv *e,const char *type,const char *message){
    jclass c=(*e)->FindClass(e,type);if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}
}
/* Convert Java UTF-16 into bounded standard UTF-8 for Vita's dialog API.
 * JNI modified UTF-8 would encode supplementary characters incorrectly. */
static int append(JNIEnv *e,jstring value,char *out,size_t capacity){
    if(!value)return 0;
    jsize length=(*e)->GetStringLength(e,value);const jchar *s=(*e)->GetStringChars(e,value,NULL);
    if(!s)return -1;
    size_t at=strlen(out);int result=0;
    for(jsize i=0;i<length;i++){
        uint32_t ch=s[i];
        if(ch>=0xd800&&ch<=0xdbff&&i+1<length&&s[i+1]>=0xdc00&&s[i+1]<=0xdfff){ch=0x10000+((ch-0xd800)<<10)+(s[++i]-0xdc00);}
        else if(!ch||(ch>=0xd800&&ch<=0xdfff))ch=0xfffd;
        unsigned n=ch<0x80?1:ch<0x800?2:ch<0x10000?3:4;
        if(n>=capacity-at){result=-1;break;}
        if(n==1)out[at++]=(char)ch;
        else{
            out[at++]=(char)((n==2?0xc0:n==3?0xe0:0xf0)|(ch>>(6*(n-1))));
            for(unsigned j=n-1;j;j--)out[at++]=(char)(0x80|((ch>>(6*(j-1)))&63));
        }
    }
    out[at]=0;(*e)->ReleaseStringChars(e,value,s);
    if(result)error(e,"java/lang/IllegalArgumentException","Popup text exceeds the bounded dialog capacity");
    return result;
}
static jvalue call(JNIEnv *e,jobject receiver,const jvalue *args,unsigned argc,void *opaque){
    (void)receiver;(void)argc;acp_popup_binding *binding=opaque;acp_popup *p=binding->owner;jvalue result={0};
    unsigned op=binding->operation;
    if(op==SHOW){
        acp_popup_request request={.id=args[0].i};
        if(append(e,args[1].l,request.text,sizeof(request.text)))return result;
        size_t n=strlen(request.text);
        if(n&&args[2].l){if(n+2>=sizeof(request.text)){error(e,"java/lang/IllegalArgumentException","Popup title too long");return result;}strcat(request.text,"\n\n");}
        if(append(e,args[2].l,request.text,sizeof(request.text)))return result;
        for(unsigned i=0;i<3;i++)if(append(e,args[3+i].l,request.buttons[i],sizeof(request.buttons[i])))return result;
        p->ops.lock(p->ops.ctx);int slot=-1,duplicate=0;
        for(unsigned i=0;i<ACP_POPUP_SLOTS;i++){
            if(!p->slots[i].state&&slot<0)slot=(int)i;
            if(p->slots[i].state&&p->slots[i].request.id==request.id)duplicate=1;
        }
        if(slot>=0&&!duplicate){p->slots[slot].request=request;p->slots[slot].response=0;p->slots[slot].state=QUEUED;p->slots[slot].order=++p->next_order;}
        p->ops.unlock(p->ops.ctx);
        if(duplicate)error(e,"java/lang/IllegalStateException","Popup id is already in use");
        else if(slot<0)error(e,"java/lang/OutOfMemoryError","Popup queue is full");
    }else if(op==GET||op==RESET){
        p->ops.lock(p->ops.ctx);
        for(unsigned i=0;i<ACP_POPUP_SLOTS;i++)if(p->slots[i].state&&p->slots[i].request.id==args[0].i){
            if(op==GET)result.i=(jint)p->slots[i].response;
            else if(p->slots[i].state==DONE)memset(&p->slots[i],0,sizeof(p->slots[i]));
            break;
        }
        p->ops.unlock(p->ops.ctx);
    }else if(op==SCHEDULE){
        p->ops.lock(p->ops.ctx);unsigned logged=p->notifications_logged;p->notifications_logged=1;p->ops.unlock(p->ops.ctx);
        if(!logged&&p->ops.log)p->ops.log(p->ops.ctx,"Local notification scheduling is unavailable on Vita");
    }
    /* Java lifecycle hooks need no Android UI thread here. Notification
     * cancellation has no work because no notifications are scheduled. */
    return result;
}
int acp_popup_configure(acp_popup *p,acp_jni_runtime *runtime,const acp_popup_ops *ops){
    if(!p||!runtime||!ops||!ops->lock||!ops->unlock||!ops->start||!ops->poll)return -1;
    memset(p,0,sizeof(*p));p->ops=*ops;
    if(acp_jni_runtime_class(runtime,ACP_POPUP_CLASS,"java/lang/Object"))return -1;
    static const struct {const char *name,*sig;int is_static;} definitions[]={
        {"ShowPopup",ACP_POPUP_SHOW_SIG,1},{"GetResponse","(I)I",1},{"ResetResponse","(I)V",1},
        {"onCreate","()V",1},{"onResume","()V",1},{"<init>","()V",0},
        {"ScheduleLocalNotification","(ILjava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;ZIIIIIII)V",1},
        {"CancelLocalNotification","(I)V",1},{"CancelAllLocalNotifications","()V",1}};
    for(unsigned i=0;i<9;i++){
        p->bindings[i]=(acp_popup_binding){p,i};
        acp_jni_method_def d={ACP_POPUP_CLASS,definitions[i].name,definitions[i].sig,definitions[i].is_static,call,&p->bindings[i]};
        if(acp_jni_runtime_methods(runtime,&d,1))return -1;
    }
    return 0;
}
int acp_popup_frame(acp_popup *p){
    if(!p||!p->ops.lock)return 0;
    p->ops.lock(p->ops.ctx);
    if(p->active){
        int response=p->ops.poll(p->ops.ctx);
        if(response){unsigned slot=p->active-1;p->slots[slot].response=response>=1&&response<=3?(unsigned)response:1;p->slots[slot].state=DONE;p->active=0;}
    }
    if(!p->active){
        int next=-1;
        for(unsigned i=0;i<ACP_POPUP_SLOTS;i++)if(p->slots[i].state==QUEUED&&(next<0||p->slots[i].order<p->slots[next].order))next=(int)i;
        if(next>=0){
            int status=p->ops.start(p->ops.ctx,&p->slots[next].request);
            if(!status){p->slots[next].state=ACTIVE;p->active=(unsigned)next+1;}
            else if(status<0){p->slots[next].state=DONE;p->slots[next].response=1;}
        }
    }
    int active=!!p->active;p->ops.unlock(p->ops.ctx);return active;
}
