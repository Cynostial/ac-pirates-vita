#include "acp_glsl_compat.h"
#include <string.h>
static int letter(unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
static int identifier(unsigned char c){return letter(c)||(c>='0'&&c<='9');}
unsigned acp_glsl_identifiers(char *s,size_t size){
    unsigned changed=0;
    for(size_t i=0;i<size;){
        if(s[i]=='/'&&i+1<size&&s[i+1]=='/'){
            i+=2;while(i<size&&s[i]!='\n')i++;
        }else if(s[i]=='/'&&i+1<size&&s[i+1]=='*'){
            i+=2;while(i+1<size&&(s[i]!='*'||s[i+1]!='/'))i++;
            i=i+1<size?i+2:size;
        }else if(s[i]=='"'||s[i]=='\''){
            char quote=s[i++];
            while(i<size){
                if(s[i]=='\\'&&i+1<size){i+=2;continue;}
                if(s[i++]==quote)break;
            }
        }else if(letter((unsigned char)s[i])){
            size_t start=i++;while(i<size&&identifier((unsigned char)s[i]))i++;
            if(i-start==7&&!memcmp(s+start,"texture",7)){
                memcpy(s+start,"vgl_tex",7);changed++;
            }
        }else i++;
    }
    return changed;
}
