#include "ce_protocol.h"
#include <string.h>
static uint16_t get16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t get32(const uint8_t *p){return get16(p)|((uint32_t)get16(p+2)<<16);}
static void put16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
static bool valid(const card_emu_config_t *c) {
    if(c->ndef_len>CARD_EMU_MAX_NDEF || !CardEmu_Is_Ndef(c->ndef,c->ndef_len) ||
        c->rule_count>CARD_EMU_MAX_RULES ||
        !((c->fallback[0]==0x90 && c->fallback[1]==0) || (c->fallback[0]==0x6A && c->fallback[1]==0x82)))return false;
    for(unsigned i=0;i<c->rule_count;++i)
        if(!c->rules[i].match_len || c->rules[i].match_len>CARD_EMU_MAX_MATCH ||
            c->rules[i].response_len<2 || c->rules[i].response_len>CARD_EMU_MAX_RESPONSE)return false;
    return true;
}
bool CE_Decode_Request(const uint8_t *p,size_t n,ce_request_t *q) {
    memset(q,0,sizeof(*q));if(!p || n<5)return false;
    q->id=get32(p);q->action=p[4];if(!q->id || q->action>CE_APPLY)return false;
    if(q->action!=CE_APPLY)return n==5;
    if(n<15 || p[9]>1)return false;
    memcpy(q->config.uid,p+5,4);q->config.write_enabled=p[9]!=0;
    q->config.ndef_len=get16(p+10);size_t off=12;
    if(q->config.ndef_len>CARD_EMU_MAX_NDEF || n-off<q->config.ndef_len+3u)return false;
    memcpy(q->config.ndef,p+off,q->config.ndef_len);off+=q->config.ndef_len;
    memcpy(q->config.fallback,p+off,2);off+=2;q->config.rule_count=p[off++];
    if(q->config.rule_count>CARD_EMU_MAX_RULES)return false;
    for(unsigned i=0;i<q->config.rule_count;++i) {
        if(n-off<3 || p[off]>1)return false;
        card_emu_rule_t *r=&q->config.rules[i];r->prefix=p[off++]!=0;r->match_len=p[off++];r->response_len=p[off++];
        if(r->match_len>CARD_EMU_MAX_MATCH || r->response_len>CARD_EMU_MAX_RESPONSE ||
            n-off<(size_t)r->match_len+r->response_len)return false;
        memcpy(r->match,p+off,r->match_len);off+=r->match_len;
        memcpy(r->response,p+off,r->response_len);off+=r->response_len;
    }
    return off==n && valid(&q->config);
}
size_t CE_Encode_Request(const ce_request_t *q,uint8_t *p,size_t cap) {
    if(!q || !p || cap<5 || !q->id || q->action>CE_APPLY)return 0;
    put32(p,q->id);p[4]=q->action;if(q->action!=CE_APPLY)return 5;
    if(!valid(&q->config))return 0;
    size_t total=15+q->config.ndef_len;
    for(unsigned i=0;i<q->config.rule_count;++i)total+=3+q->config.rules[i].match_len+q->config.rules[i].response_len;
    if(total>cap)return 0;
    memcpy(p+5,q->config.uid,4);p[9]=q->config.write_enabled;put16(p+10,q->config.ndef_len);
    size_t off=12;memcpy(p+off,q->config.ndef,q->config.ndef_len);off+=q->config.ndef_len;
    memcpy(p+off,q->config.fallback,2);off+=2;p[off++]=q->config.rule_count;
    for(unsigned i=0;i<q->config.rule_count;++i) {
        const card_emu_rule_t *r=&q->config.rules[i];p[off++]=r->prefix;p[off++]=r->match_len;p[off++]=r->response_len;
        memcpy(p+off,r->match,r->match_len);off+=r->match_len;memcpy(p+off,r->response,r->response_len);off+=r->response_len;
    }
    return off;
}
void CE_Encode_Reply(uint32_t id,uint8_t status,uint8_t mode,const uint8_t uid[4],uint16_t error,uint8_t p[CE_REPLY_BYTES]) {
    memset(p,0,CE_REPLY_BYTES);put32(p,id);p[4]=status;p[5]=mode;if(uid)memcpy(p+6,uid,4);put16(p+10,error);
}
