#include "advanced_protocol.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t u32(const uint8_t *p){return u16(p)|(uint32_t)u16(p+2)<<16;}
static void put16(uint8_t *p,uint16_t n){p[0]=n;p[1]=n>>8;}
static void put32(uint8_t *p,uint32_t n){put16(p,n);put16(p+2,n>>16);}
bool Advanced_Target_Equal(const advanced_target_t *a,const advanced_target_t *b){
    return a && b && a->available && b->available && a->token && a->token==b->token &&
        a->uid_len && a->uid_len<=10 && a->uid_len==b->uid_len &&
        !memcmp(a->uid,b->uid,a->uid_len) && a->protocol==b->protocol && a->subtype==b->subtype;
}
void Advanced_Encode_Target(const advanced_target_t *t,uint8_t out[ADV_TARGET_BYTES]){
    memset(out,0,ADV_TARGET_BYTES);put32(out,t->token);out[4]=t->uid_len;
    memcpy(out+5,t->uid,10);out[15]=t->protocol;out[16]=t->subtype;
    put16(out+17,t->first);put16(out+19,t->last);out[21]=t->unit;out[22]=t->available;
}
bool Advanced_Decode_Request(const uint8_t *p,size_t n,advanced_request_t *q){
    memset(q,0,sizeof(*q));if(n>=4)q->id=u32(p);
    if(n<ADV_HEADER_BYTES)return false;
    q->target.token=u32(p+4);q->target.uid_len=p[8];memcpy(q->target.uid,p+9,10);
    q->target.protocol=p[19];q->target.subtype=p[20];q->target.available=1;
    q->operation=p[21];q->flags=p[22];q->address=u16(p+23);q->count=u16(p+25);
    q->key_type=p[27];memcpy(q->key,p+28,6);
    /* Header is 36 bytes: fixed identity + action + key + data length. */
    q->length=u16(p+34);
    if(!q->id || !q->target.token || !q->target.uid_len || q->target.uid_len>10 ||
       q->length>ADV_MAX_DATA || n!=36u+q->length || q->flags&~7u)return false;
    for(unsigned x=q->target.uid_len;x<10;x++)if(q->target.uid[x])return false;
    memcpy(q->data,p+36,q->length);return true;
}
uint8_t Advanced_Validate(const advanced_target_t *t,const advanced_request_t *q){
    if(!Advanced_Target_Equal(t,&q->target))return STATUS_CARD_MISMATCH;
    if(!t->subtype || !t->unit || !q->count || q->address<t->first ||
       (uint32_t)q->address+q->count-1>(q->operation==ADV_SCAN && t->subtype==ADV_CLASSIC_4K?255:t->last) || (uint32_t)q->count*t->unit>ADV_MAX_DATA)return ADV_INVALID;
    bool classic=t->subtype>=ADV_CLASSIC_1K && t->subtype<=ADV_CLASSIC_MINI;
    bool t2=t->subtype>=ADV_NTAG213 && t->subtype<=ADV_ULTRALIGHT;
    if(t->subtype>ADV_ISO15693_4 ||
       (classic && (t->unit!=16 || t->first || t->last>(t->subtype==ADV_CLASSIC_MINI?19:63))) ||
       (t2 && (t->unit!=4 || t->uid_len!=7 || t->first!=4 || t->last>225)) ||
       (t->subtype==ADV_LITE_S && (t->unit!=16 || t->uid_len!=8 || t->first || t->last!=13)) ||
       (t->subtype==ADV_ISO15693_4 && (t->unit!=4 || t->uid_len!=8 || t->first || t->last>255)))return ADV_INVALID;
    if((t->subtype==ADV_NTAG213 && t->last!=39) || (t->subtype==ADV_NTAG215 && t->last!=129) ||
       (t->subtype==ADV_NTAG216 && t->last!=225))return ADV_INVALID;
    if((classic && (t->protocol!=0x80 || (q->key_type!=0x60 && q->key_type!=0x61))) ||
       (t2 && t->protocol!=2) || (t->subtype==ADV_LITE_S && t->protocol!=3) ||
       (t->subtype==ADV_ISO15693_4 && t->protocol!=6))return ADV_INVALID;
    if(q->operation==ADV_READ)return !q->length && (t2 || q->count==1)?ADV_OK:ADV_INVALID;
    if(q->operation==ADV_SCAN){
        /* Read-only scan may cover all 4K sectors; regular writes retain the
         * original 0..63 restriction. Each Classic chunk stays in one sector. */
        unsigned width=q->address<128?4:16;
        return !q->length && (!classic || (q->address+q->count-1<128)==(q->address<128)) &&
            (!classic || q->address/width==(q->address+q->count-1)/width)?ADV_OK:ADV_INVALID;
    }
    if(q->operation==ADV_UID_WRITE){
        return classic && q->address==0 && q->count==1 && t->uid_len==4 && q->length==4 &&
            (q->flags&(ADV_CONFIRM_GEN2|ADV_CONFIRM_UID))==(ADV_CONFIRM_GEN2|ADV_CONFIRM_UID) &&
            (q->data[0]|q->data[1]|q->data[2]|q->data[3]) ? ADV_OK:ADV_PROTECTED;
    }
    if(q->operation==ADV_NDEF){
        if(!t2 || q->address!=4 || q->length!=q->count*4 || q->length<8 || q->data[0]!=3 ||
           q->data[1]<5 || q->data[1]>251 || (unsigned)q->data[1]+3>q->length ||
           q->data[2]!=0xD1 || q->data[3]!=1 || q->data[4]+4!=q->data[1] ||
           (q->data[5]!='U' && q->data[5]!='T') || q->data[2+q->data[1]]!=0xFE)return ADV_INVALID;
        for(unsigned x=3+q->data[1];x<q->length;x++)if(q->data[x])return ADV_INVALID;
        return ADV_OK;
    }
    if(q->operation!=ADV_WRITE || q->count!=1 || q->length!=t->unit)return ADV_INVALID;
    if(classic && (!q->address || (q->address%4==3 && !(q->flags&ADV_CONFIRM_TRAILER))))return ADV_PROTECTED;
    if(classic && q->address%4==3){
        /* ISO14443 Classic access-bit complements (bytes 6..8). */
        const uint8_t *d=q->data;
        if(((d[6]&15)^(d[7]>>4))!=15 || ((d[6]>>4)^(d[8]&15))!=15 ||
           ((d[7]&15)^(d[8]>>4))!=15)return ADV_INVALID;
    }
    return ADV_OK;
}
size_t Advanced_Encode_Result(const advanced_result_t *r,uint8_t *out,size_t cap){
    /* id4 token4 uidlen1 uid10 protocol1 subtype1 op1 status1 detail1
     * address2 completed2 length2 = 30 bytes. */
    if(r->length>ADV_MAX_DATA || cap<30u+r->length)return 0;
    put32(out,r->id);put32(out+4,r->target.token);out[8]=r->target.uid_len;
    memcpy(out+9,r->target.uid,10);out[19]=r->target.protocol;out[20]=r->target.subtype;
    out[21]=r->operation;out[22]=r->status;out[23]=r->detail;put16(out+24,r->address);
    put16(out+26,r->completed);put16(out+28,r->length);memcpy(out+30,r->data,r->length);
    return 30u+r->length;
}
