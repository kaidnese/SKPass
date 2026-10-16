#include "cards_json.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
typedef struct {size_t start,end;int next;char kind;} Token;
typedef struct {const char *s;size_t n,p;int used;Token t[4096];} Parser;
static void ws(Parser *p){while(p->p<p->n && strchr(" \r\n\t",p->s[p->p]))p->p++;}
static int value(Parser *p,int depth) {
    if(depth>32 || p->used>=4096)return -1;ws(p);if(p->p>=p->n)return -1;
    int i=p->used++;Token *t=&p->t[i];t->start=p->p;t->kind=p->s[p->p++];
    if(t->kind=='{' || t->kind=='[') {
        char close=t->kind=='{'?'}':']';ws(p);
        if(p->p<p->n && p->s[p->p]==close)p->p++;
        else for(;;) {
            if(t->kind=='{') {int k=value(p,depth+1);if(k<0 || p->t[k].kind!='"')return -1;ws(p);if(p->p>=p->n || p->s[p->p++]!=':')return -1;}
            if(value(p,depth+1)<0)return -1;ws(p);if(p->p>=p->n)return -1;
            char c=p->s[p->p++];if(c==close)break;if(c!=',')return -1;
        }
    } else if(t->kind=='"') {
        bool done=false;
        while(p->p<p->n){unsigned char c=(unsigned char)p->s[p->p++];if(c=='"'){done=true;break;}if(c<32)return -1;
            if(c=='\\'){if(p->p>=p->n)return -1;c=(unsigned char)p->s[p->p++];if(c=='u'){for(int k=0;k<4;k++)if(p->p>=p->n || !isxdigit((unsigned char)p->s[p->p++]))return -1;}else if(!strchr("\"\\/bfnrt",c))return -1;}
        }if(!done)return -1;
    } else {
        while(p->p<p->n && !strchr(" \r\n\t,]}",p->s[p->p]))p->p++;
        size_t len=p->p-t->start;const char *s=p->s+t->start;
        if(!((len==4 && !memcmp(s,"true",4)) || (len==5 && !memcmp(s,"false",5)) || (len==4 && !memcmp(s,"null",4)))) {
            size_t k=0;if(k<len && s[k]=='-')k++;if(k==len)return -1;
            if(s[k]=='0')k++;else {if(s[k]<'1'||s[k]>'9')return -1;while(k<len && isdigit((unsigned char)s[k]))k++;}
            if(k<len && s[k]=='.'){k++;size_t b=k;while(k<len && isdigit((unsigned char)s[k]))k++;if(k==b)return -1;}
            if(k<len && (s[k]=='e'||s[k]=='E')){k++;if(k<len && (s[k]=='+'||s[k]=='-'))k++;size_t b=k;while(k<len && isdigit((unsigned char)s[k]))k++;if(k==b)return -1;}
            if(k!=len)return -1;
        }
    }
    t->end=p->p;t->next=p->used;return i;
}
static bool str_eq(Parser *p,int i,const char *s){Token *t=&p->t[i];return t->kind=='"' && t->end-t->start==strlen(s)+2 && !memcmp(p->s+t->start+1,s,strlen(s));}
static int field(Parser *p,int i,const char *name) {
    if(p->t[i].kind!='{')return -1;
    int result=-1;for(int k=i+1;k<p->t[i].next;){int v=k+1;if(str_eq(p,k,name)){if(result>=0)return -1;result=v;}k=p->t[v].next;}return result;
}
bool cards_json_parse(const char *text,size_t size,uint8_t code[10],unsigned *key) {
    Parser p={.s=text,.n=size};if(value(&p,0)!=0 || p.t[0].kind!='{')return false;ws(&p);if(p.p!=size)return false;
    int version=field(&p,0,"version"),a=field(&p,0,"activeCode"),k=field(&p,0,"keyVk"),list=field(&p,0,"cards");
    if(version<0 || p.t[version].kind!='1' || p.t[version].end-p.t[version].start!=1 || k<0 || a<0 || list<0 || p.t[list].kind!='[')return false;
    if(p.t[k].kind<'0'||p.t[k].kind>'9')return false;
    size_t kn=p.t[k].end-p.t[k].start;if(kn>3)return false;unsigned vk=0;for(size_t z=0;z<kn;z++){char c=text[p.t[k].start+z];if(c<'0'||c>'9')return false;vk=vk*10+c-'0';}
    if(!(vk==8 || vk==9 || vk==13 || vk==32 || (vk>=0x21 && vk<=0x28) || vk==0x2D || vk==0x2E ||
         (vk>=0x30 && vk<=0x39) || (vk>=0x41 && vk<=0x5A) || (vk>=0x60 && vk<=0x6F && vk!=0x6C) ||
         (vk>=0x70 && vk<=0x87) || (vk>=0xBA && vk<=0xC0) || (vk>=0xDB && vk<=0xDF) || vk==0xE2))return false;
    *key=vk;
    if(p.t[a].kind!='"' || p.t[a].end-p.t[a].start!=22)return false;
    char selected[21];memcpy(selected,text+p.t[a].start+1,20);selected[20]=0;bool nonzero=false;
    for(int z=0;z<20;z++){if(selected[z]<'0'||selected[z]>'9')return false;if(selected[z]!='0')nonzero=true;}if(!nonzero)return false;
    bool found=false;for(int z=list+1;z<p.t[list].next;z=p.t[z].next){int c=field(&p,z,"accessCode"),t=field(&p,z,"type");if(c>=0 && t>=0 && str_eq(&p,t,"classic") && str_eq(&p,c,selected))found=true;}
    if(!found)return false;
    for(int z=0;z<10;z++)code[z]=(uint8_t)((selected[2*z]-'0')*16+selected[2*z+1]-'0');return true;
}
