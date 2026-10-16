#include "card_emulation.h"
#include <string.h>

static const uint8_t ndef_aid[] = {0xD2,0x76,0x00,0x00,0x85,0x01,0x01};
static const uint8_t cc_file[] = {
    0x00,0x0F,0x20,0x01,0x00,0x00,0xFF,0x04,0x06,0xE1,0x04,
    0x00,0xF2,0x00,0x00 /* NDEF file = 2-byte NLEN + 240 bytes */
};
static size_t status(uint8_t *out,uint16_t sw) {
    out[0]=(uint8_t)(sw>>8);out[1]=(uint8_t)sw;return 2;
}
void CardEmu_Reset_Session(card_emu_t *e) {
    e->app_selected=false;e->selected_file=0;e->update_active=false;
    e->expected_len=0;memset(e->written,0,sizeof(e->written));
}
void CardEmu_Init(card_emu_t *e,const card_emu_config_t *c) {
    memset(e,0,sizeof(*e));if(c)e->config=*c;
}
void CardEmu_Set_Config(card_emu_t *e,const card_emu_config_t *c) {
    CardEmu_Init(e,c);
}
bool CardEmu_Is_Ndef(const uint8_t *d,size_t n) {
    if(!n)return true; /* empty NDEF file */
    if(!d || n>CARD_EMU_MAX_NDEF)return false;
    size_t p=0;bool first=true;
    while(p<n) {
        uint8_t h=d[p++];uint8_t tnf=h&7;
        if((bool)(h&0x80)!=first || (h&0x20) || tnf==6 || tnf==7 || p>=n)return false;
        size_t type=d[p++],payload=0,id=0;
        if(h&0x10){if(p>=n)return false;payload=d[p++];}
        else {
            if(n-p<4)return false;
            payload=((uint32_t)d[p]<<24)|((uint32_t)d[p+1]<<16)|((uint32_t)d[p+2]<<8)|d[p+3];p+=4;
        }
        if(h&0x08){if(p>=n)return false;id=d[p++];}
        if(type>n-p)return false;
        p+=type;
        if(id>n-p)return false;
        p+=id;
        if(payload>n-p)return false;
        p+=payload;
        if(tnf==0 && (type || id || payload))return false;
        if(tnf==5 && type)return false;
        if(h&0x40)return p==n;
        first=false;
    }
    return false;
}
size_t CardEmu_Build_Ndef(uint8_t kind,const char *text,uint8_t *out,size_t cap) {
    if(!text || !out || kind>1)return 0;
    size_t n=strlen(text);
    if(kind==0) {
        uint8_t code=0;const char *s=text;
        if(!strncmp(s,"https://",8)){code=4;s+=8;}
        else if(!strncmp(s,"http://",7)){code=3;s+=7;}
        n=strlen(s);if(n+5>cap || n+5>CARD_EMU_MAX_NDEF)return 0;
        out[0]=0xD1;out[1]=1;out[2]=(uint8_t)(n+1);out[3]='U';out[4]=code;
        memcpy(out+5,s,n);return n+5;
    }
    if(n+7>cap || n+7>CARD_EMU_MAX_NDEF)return 0;
    out[0]=0xD1;out[1]=1;out[2]=(uint8_t)(n+3);out[3]='T';
    out[4]=2;out[5]='e';out[6]='n';memcpy(out+7,text,n);return n+7;
}
static bool short_data(const uint8_t *a,size_t n,size_t *lc) {
    if(n<5 || !a[4])return false;
    *lc=a[4];return n==5+*lc || n==6+*lc;
}
static uint8_t file_byte(const card_emu_t *e,size_t off) {
    if(e->selected_file==1) {
        if(off==14 && !e->config.write_enabled)return 0xFF;
        return cc_file[off];
    }
    if(off==0)return e->update_active?0:e->config.ndef_len>>8;
    if(off==1)return e->update_active?0:(uint8_t)e->config.ndef_len;
    return e->config.ndef[off-2];
}
static size_t update(card_emu_t *e,const uint8_t *a,size_t n,uint8_t *r,bool *changed) {
    if(!e->app_selected || e->selected_file!=2)return status(r,0x6986);
    if(!e->config.write_enabled)return status(r,0x6982);
    if(n<5 || n!=5u+a[4] || !a[4])return status(r,0x6700);
    size_t off=((size_t)a[2]<<8)|a[3],lc=a[4];
    if(off>CARD_EMU_MAX_NDEF+2 || lc>CARD_EMU_MAX_NDEF+2-off)return status(r,0x6B00);
    if(off==1 || (off==0 && lc<2))return status(r,0x6B00);
    const uint8_t *data=a+5;
    uint16_t length=off==0?((uint16_t)data[0]<<8)|data[1]:0;
    if(length>CARD_EMU_MAX_NDEF)return status(r,0x6A84);
    if(off==0 && length==0) {
        memset(e->written,0,sizeof(e->written));e->update_active=true;
        e->expected_len=0;
        if(e->config.ndef_len && changed)*changed=true;
        e->config.ndef_len=0;
    }
    /* A complete file in one UPDATE is valid too. Existing committed data
     * is kept until a nonzero NLEN has validated the entire staged message. */
    if(off==0 && lc>2 && !e->update_active) {
        memset(e->written,0,sizeof(e->written));e->update_active=true;
    }
    size_t p=off==0?0:off-2;
    size_t count=off==0?lc-2:lc;
    if(count && !e->update_active)return status(r,0x6985);
    if(count) {
        memcpy(e->staged+p,data+(off==0?2:0),count);
        for(size_t i=p;i<p+count;++i)e->written[i/8]|=(uint8_t)(1u<<(i%8));
    }
    if(off==0 && length) {
        if(!e->update_active)return status(r,0x6985);
        for(size_t i=0;i<length;++i)if(!(e->written[i/8]&(1u<<(i%8))))return status(r,0x6985);
        if(!CardEmu_Is_Ndef(e->staged,length))return status(r,0x6A80);
        memcpy(e->config.ndef,e->staged,length);e->config.ndef_len=length;
        e->update_active=false;if(changed)*changed=true;
    }
    return status(r,0x9000);
}
size_t CardEmu_Process_Apdu(card_emu_t *e,const uint8_t *a,size_t n,
                            uint8_t *r,size_t cap,bool *changed) {
    if(changed)*changed=false;
    if(!e || !a || !r || cap<2)return 0;
    if(n<4)return status(r,0x6700);
    if(a[0]==0 && a[1]==0xA4) {
        size_t lc=0;
        if(!short_data(a,n,&lc))return status(r,0x6700);
        if(a[2]==4 && (a[3]==0 || a[3]==0x0C)) {
            e->app_selected=false;e->selected_file=0;
            if(lc==sizeof(ndef_aid) && !memcmp(a+5,ndef_aid,sizeof(ndef_aid))) {
                CardEmu_Reset_Session(e);e->app_selected=true;return status(r,0x9000);
            }
            /* A SELECT for another application is eligible for custom rules. */
        } else if(a[2]==0 && (a[3]==0x0C || a[3]==0)) {
            if(!e->app_selected)return status(r,0x6986);
            if(lc!=2)return status(r,0x6700);
            uint16_t id=((uint16_t)a[5]<<8)|a[6];
            e->selected_file=id==0xE103?1:id==0xE104?2:0;
            return status(r,e->selected_file?0x9000:0x6A82);
        } else if(e->app_selected)return status(r,0x6A86);
    }
    if(a[0]==0 && a[1]==0xB0 && e->app_selected) {
        if(n!=5)return status(r,0x6700);
        if(!e->selected_file)return status(r,0x6986);
        size_t off=((size_t)a[2]<<8)|a[3],want=a[4]?a[4]:256;
        size_t total=e->selected_file==1?sizeof(cc_file):2u+e->config.ndef_len;
        if(off>=total)return status(r,0x6B00);
        size_t count=want<total-off?want:total-off;
        if(count+2>cap)return status(r,0x6C00);
        for(size_t i=0;i<count;++i)r[i]=file_byte(e,off+i);
        r[count]=count<want?0x62:0x90;r[count+1]=count<want?0x82:0;
        return count+2;
    }
    if(a[0]==0 && a[1]==0xD6 && e->app_selected)return update(e,a,n,r,changed);
    for(unsigned i=0;i<e->config.rule_count && i<CARD_EMU_MAX_RULES;++i) {
        const card_emu_rule_t *q=&e->config.rules[i];
        if(q->match_len && n>=q->match_len && (q->prefix || n==q->match_len) &&
            !memcmp(a,q->match,q->match_len)) {
            if(q->response_len<2 || q->response_len>cap)return status(r,0x6F00);
            memcpy(r,q->response,q->response_len);return q->response_len;
        }
    }
    return status(r,((uint16_t)e->config.fallback[0]<<8)|e->config.fallback[1]);
}
