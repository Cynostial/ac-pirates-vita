#include "acp_popup.h"
#include "acp_native.h"
#include <psp2/message_dialog.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static acp_popup popup;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static const acp_popup_request *request;
static size_t offset,next_offset;
static char page[SCE_MSG_DIALOG_USER_MSG_SIZE];
static int more,selection[3];
static SceMsgDialogButtonsParam buttons;
static void lock(void *ctx){(void)ctx;pthread_mutex_lock(&mutex);}
static void unlock(void *ctx){(void)ctx;pthread_mutex_unlock(&mutex);}
static void log_message(void *ctx,const char *message){(void)ctx;acp_log("%s",message);}
static int open_page(void){
    memset(&buttons,0,sizeof(buttons));memset(selection,0,sizeof(selection));
    const char *labels[3]={0};unsigned count=0;
    for(unsigned i=0;i<3;i++)if(request->buttons[i][0]){labels[count]=request->buttons[i];selection[count++]=(int)i+1;}
    if(!count){labels[0]="Close";selection[0]=1;count=1;}
    char footer[288]={0};int type=SCE_MSG_DIALOG_BUTTON_TYPE_3BUTTONS;
    if(count==1){type=SCE_MSG_DIALOG_BUTTON_TYPE_OK;snprintf(footer,sizeof(footer),"\n\nOK: %s",labels[0]);}
    else if(count==2){
        if(selection[0]==1){
            type=SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL;
            snprintf(footer,sizeof(footer),"\n\nOK: %s\nCancel: %s",labels[1],labels[0]);
            selection[0]=selection[1];selection[1]=1;
        }else{
            type=SCE_MSG_DIALOG_BUTTON_TYPE_YESNO;
            snprintf(footer,sizeof(footer),"\n\nYes: %s\nNo: %s",labels[0],labels[1]);
        }
    }else{buttons.msg1=labels[0];buttons.msg2=labels[1];buttons.msg3=labels[2];}
    const char *text=request->text+offset;size_t length=strlen(text),take=length;
    more=length+strlen(footer)>=sizeof(page);
    if(more){
        strcpy(footer,"\n\nOK: Next page\nCancel: Close");
        size_t space=sizeof(page)-strlen(footer)-1;if(take>space)take=space;
        type=SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL;selection[0]=0;selection[1]=1;
    }
    /* UTF-8 pages end on complete characters. Intermediate pages never return
     * a game choice. Three custom labels are used only when all three exist. */
    while(take&&((unsigned char)text[take]&0xc0)==0x80)take--;
    memcpy(page,text,take);page[take]=0;strcat(page,footer);next_offset=offset+take;
    SceMsgDialogUserMessageParam message={0};
    message.buttonType=type;message.msg=(const void *)page;message.buttonParam=type==SCE_MSG_DIALOG_BUTTON_TYPE_3BUTTONS?&buttons:NULL;
    SceMsgDialogParam param;sceMsgDialogParamInit(&param);param.mode=SCE_MSG_DIALOG_MODE_USER_MSG;param.userMsgParam=&message;
    int result=sceMsgDialogInit(&param);
    if(result<0)acp_log("Popup %d could not open: 0x%08x",request->id,(unsigned)result);
    return result<0?-1:0;
}
static int start(void *ctx,const acp_popup_request *value){
    (void)ctx;
    if(sceMsgDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_NONE)return 1;
    request=value;offset=0;
    int result=open_page();if(result<0)request=NULL;
    return result;
}
static int poll(void *ctx){
    (void)ctx;
    if(sceMsgDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return 0;
    SceMsgDialogResult result={0};int status=sceMsgDialogGetResult(&result);sceMsgDialogTerm();
    if(status<0){acp_log("Popup result failed: 0x%08x",(unsigned)status);request=NULL;return -1;}
    int button=result.buttonId;
    if(more&&result.result==SCE_COMMON_DIALOG_RESULT_OK&&button==1){offset=next_offset;if(!open_page())return 0;request=NULL;return -1;}
    int response=result.result==SCE_COMMON_DIALOG_RESULT_OK&&button>=1&&button<=3?selection[button-1]:1;
    if(!response)response=1;
    acp_log("Popup %d completed with response %d",request->id,response);request=NULL;return response;
}
int acp_popup_vita_configure(acp_jni_runtime *runtime){
    acp_popup_ops ops={NULL,lock,unlock,start,poll,log_message};return acp_popup_configure(&popup,runtime,&ops);
}
int acp_popup_vita_frame(void){return acp_popup_frame(&popup);}
