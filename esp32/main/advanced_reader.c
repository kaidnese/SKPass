#include "advanced_reader.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
void Advanced_Describe(const pn7150_card_info_t *c,const smartcard_info_t *i,advanced_target_t *t){
    memset(t,0,sizeof(*t));if(!c || !i || c->uid_length>10 || !c->uid_length)return;
    t->uid_len=c->uid_length;memcpy(t->uid,c->uid,t->uid_len);t->protocol=c->protocol;
    t->available=1;
    if(c->protocol==0x80 && c->rf_interface==0x80 && c->sak_valid){
        if(c->sak==8)t->subtype=ADV_CLASSIC_1K;
        else if(c->sak==0x18)t->subtype=ADV_CLASSIC_4K;
        else if(c->sak==9)t->subtype=ADV_CLASSIC_MINI;
        t->last=c->sak==9?19:63;t->unit=16;
    }else if(c->protocol==2 && c->rf_interface==1 && i->recognized && i->t2_writable && i->pages && i->last_page>=4){
        if(!strcmp(i->kind,"ntag213"))t->subtype=ADV_NTAG213;
        else if(!strcmp(i->kind,"ntag215"))t->subtype=ADV_NTAG215;
        else if(!strcmp(i->kind,"ntag216"))t->subtype=ADV_NTAG216;
        else if(!strcmp(i->kind,"ultralight"))t->subtype=ADV_ULTRALIGHT;
        t->first=4;t->last=i->last_page;t->unit=4;
    }else if(c->protocol==3 && c->rf_interface==1 && c->uid_length==8 && c->pmm_valid &&
        (c->pmm[1]==0xF1 || c->pmm[1]==0xF2) && i->lite_s_verified &&
        strncmp(i->kind,"transit_",8)){
        t->subtype=ADV_LITE_S;t->last=13;t->unit=16;
    }else if(c->protocol==6 && c->rf_interface==1 && c->uid_length==8 &&
        i->recognized && i->blocks && i->block_size==4){
        t->subtype=ADV_ISO15693_4;t->last=i->blocks-1;t->unit=4;
    }
    if(i->session_failed)t->subtype=0;
}
static bool xchg(i2c_master_bus_handle_t bus,const uint8_t *cmd,size_t size,
                  uint8_t *rx,size_t *n,advanced_result_t *r){
    vTaskDelay(pdMS_TO_TICKS(5)+1);*n=0;
    esp_err_t ret=Pn7150_Tag_Transceive(bus,cmd,size,rx,255,n);
    r->detail=ret==ESP_OK && *n?rx[*n-1]:0xFF;
    return ret==ESP_OK;
}
static bool classic_read(i2c_master_bus_handle_t bus,uint8_t block,uint8_t *out,advanced_result_t *r){
    uint8_t cmd[]={0x10,0x30,block},rx[255];size_t n;
    if(!xchg(bus,cmd,3,rx,&n,r) || n!=18 || rx[0]!=0x10 || rx[17])return false;
    memcpy(out,rx+1,16);return true;
}
static bool classic_ack(i2c_master_bus_handle_t bus,const uint8_t *cmd,size_t size,advanced_result_t *r){
    uint8_t rx[255];size_t n;
    if(!xchg(bus,cmd,size,rx,&n,r))return false;
    return (n==2 && rx[0]==0x10 && !rx[1]) || (n==3 && rx[0]==0x10 && rx[1]==0x0A && !rx[2]);
}
static void classic(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,const advanced_request_t *q,advanced_result_t *r){
    /* Fresh anticollision identity before Crypto1; KEY B sets embedded bit 80.
     * NXP phNxpExtns_MifareStd PHNCINFC_ENABLE_KEY_B, EMBEDDED_KEY. */
    if(Pn7150_ReActivate(bus,c)!=ESP_OK){r->status=STATUS_CARD_MISMATCH;return;}
    uint8_t auth[9]={0x40,(uint8_t)(q->address<128?q->address/4:32+(q->address-128)/16),(uint8_t)(q->key_type==0x61?0x90:0x10)},rx[255];size_t n;
    memcpy(auth+3,q->key,6);r->status=ADV_AUTH_FAILED;
    if(!xchg(bus,auth,9,rx,&n,r) || n!=2 || rx[0]!=0x40 || rx[1])return;
    if(q->operation==ADV_SCAN){
        r->status=ADV_RF_FAILED;
        for(unsigned x=0;x<q->count;x++){
            if(!classic_read(bus,q->address+x,r->data+x*16,r))return;
            r->completed++;r->length+=16;
        }
        r->status=ADV_OK;return;
    }
    uint8_t before[16],desired[16];r->status=ADV_RF_FAILED;
    if(!classic_read(bus,q->address,before,r))return;
    if(q->operation==ADV_READ){memcpy(r->data,before,16);r->length=16;r->completed=1;r->status=ADV_OK;return;}
    if(q->operation==ADV_UID_WRITE){
        /* No harmless RF command proves Gen2. Preserve SAK/ATQA/manufacturer
         * bytes and require the original 4-byte UID/BCC to be self-consistent. */
        if(memcmp(before,c->uid,4) || before[4]!=(before[0]^before[1]^before[2]^before[3]) || before[5]!=c->sak ||
           (before[6]!=4 && before[6]!=2) || before[7]){r->status=ADV_PROTECTED;return;}
        memcpy(desired,before,16);memcpy(desired,q->data,4);
        desired[4]=desired[0]^desired[1]^desired[2]^desired[3];
    }else memcpy(desired,q->data,16);
    uint8_t start[]={0x10,0xA0,(uint8_t)q->address},data[17]={0x10};memcpy(data+1,desired,16);
    if(!classic_ack(bus,start,3,r) || !classic_ack(bus,data,17,r))return;
    r->completed=1;r->status=ADV_VERIFY_FAILED;
    if(q->address%4==3){
        /* A trailer changes keys and reads mask Key A; never claim a raw
         * 16-byte comparison is possible. Verify with new Key A and access bits. */
        memcpy(auth+3,desired,6);auth[2]=0x10;
        if(Pn7150_ReActivate(bus,c)!=ESP_OK || !xchg(bus,auth,9,rx,&n,r) || n!=2 || rx[0]!=0x40 || rx[1] ||
           !classic_read(bus,q->address,r->data,r) || memcmp(r->data+6,desired+6,4))return;
    }else if(!classic_read(bus,q->address,r->data,r) || memcmp(r->data,desired,16))return;
    r->length=16;r->status=ADV_OK;
}
static bool frame(i2c_master_bus_handle_t bus,const uint8_t *cmd,size_t size,uint8_t *rx,size_t *n,advanced_result_t *r){
    if(!xchg(bus,cmd,size,rx,n,r) || !*n || rx[*n-1])return false;
    --*n;return true;
}
static bool t2read(i2c_master_bus_handle_t bus,uint16_t page,uint8_t out[16],advanced_result_t *r){
    uint8_t cmd[]={0x30,(uint8_t)page},rx[255];size_t n;
    if(!frame(bus,cmd,2,rx,&n,r) || n!=16)return false;
    memcpy(out,rx,16);return true;
}
static bool t2write(i2c_master_bus_handle_t bus,uint16_t page,const uint8_t data[4],advanced_result_t *r){
    uint8_t cmd[6]={0xA2,(uint8_t)page},rx[255];size_t n;memcpy(cmd+2,data,4);
    if(!frame(bus,cmd,6,rx,&n,r) || n!=1 || rx[0]!=0x0A)return false;
    return true;
}
static bool t2identity(i2c_master_bus_handle_t bus,const advanced_target_t *t,advanced_result_t *r){
    uint8_t data[16];
    if(t->uid_len!=7 || !t2read(bus,0,data,r) || memcmp(data,t->uid,3) || memcmp(data+4,t->uid+3,4) ||
       data[3]!=(0x88^data[0]^data[1]^data[2]) || data[8]!=(data[4]^data[5]^data[6]^data[7])){
        r->status=STATUS_CARD_MISMATCH;return false;
    }
    return true;
}
static void type2(i2c_master_bus_handle_t bus,const advanced_target_t *t,const advanced_request_t *q,advanced_result_t *r){
    uint8_t data[16];r->status=ADV_RF_FAILED;
    /* READ/WRITE pages are not UID-addressed. Verify the manufacturer UID
     * immediately before access; reject unsupported UID layouts. */
    if(!t2identity(bus,t,r))return;
    if(q->operation==ADV_READ || q->operation==ADV_SCAN){
        unsigned done=0;
        while(done<q->count){
            if(done && !t2identity(bus,t,r))return;
            /* The RF READ fetches 4 pages; shift the final window back inside
             * the allowed user area, never read configuration/lock pages. */
            unsigned page=q->address+done,start=page;
            if(start+3>t->last)start=t->last-3;
            if(start<t->first || !t2read(bus,start,data,r))return;
            unsigned take=4-(page-start);if(take>q->count-done)take=q->count-done;
            memcpy(r->data+done*4,data+(page-start)*4,take*4);done+=take;r->completed=done;r->length=done*4;
        }
        r->length=q->count*4;r->status=ADV_OK;return;
    }
    /* NDEF: clear TLV length before content writes, publish first page last.
     * A failed write never triggers automatic repeat. */
    if(q->operation==ADV_NDEF){uint8_t empty[4]={3,0,0xFE,0};if(!t2write(bus,4,empty,r))return;}
    for(unsigned index=0;index<q->count;index++){
        unsigned x=q->operation==ADV_NDEF?(index+1)%q->count:index;
        unsigned page=q->address+x;
        /* Type 2 writes are unaddressed: never continue an NDEF document
         * onto a substituted card between pages. */
        if(!t2identity(bus,t,r))return;
        if(!t2write(bus,page,q->data+4*x,r))return;
        ++r->completed;r->status=ADV_VERIFY_FAILED;
        unsigned start=page+3>t->last?(unsigned)t->last-3u:page;
        if(!t2read(bus,start,data,r) || memcmp(data+4*(page-start),q->data+4*x,4))return;
        r->status=ADV_RF_FAILED;
    }
    memcpy(r->data,q->data,q->length);r->length=q->length;r->status=ADV_OK;
}
static bool felica_read(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,uint8_t b,uint8_t out[16],advanced_result_t *r){
    uint8_t cmd[16]={16,6},rx[255];size_t n;memcpy(cmd+2,c->uid,8);
    cmd[10]=1;cmd[11]=0x0B;cmd[13]=1;cmd[14]=0x80;cmd[15]=b;
    if(!xchg(bus,cmd,16,rx,&n,r) || (n!=29 && !(n==30 && !rx[29])) || rx[0]!=29 || rx[1]!=7 ||
       memcmp(rx+2,c->uid,8) || rx[10] || rx[11] || rx[12]!=1)return false;
    memcpy(out,rx+13,16);return true;
}
static void felica(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,const advanced_request_t *q,advanced_result_t *r){
    r->status=STATUS_CARD_MISMATCH;
    if(q->operation==ADV_SCAN){
        for(unsigned x=0;x<q->count;x++){
            if(!felica_read(bus,c,q->address+x,r->data+x*16,r))return;
            r->completed++;r->length+=16;r->status=ADV_RF_FAILED;
        }
        r->status=ADV_OK;return;
    }
    if(!felica_read(bus,c,q->address,r->data,r))return;
    r->status=ADV_RF_FAILED;
    if(q->operation==ADV_WRITE){
        uint8_t cmd[32]={32,8},rx[255];size_t n;memcpy(cmd+2,c->uid,8);
        cmd[10]=1;cmd[11]=9;cmd[13]=1;cmd[14]=0x80;cmd[15]=q->address;memcpy(cmd+16,q->data,16);
        if(!xchg(bus,cmd,32,rx,&n,r) || (n!=12 && !(n==13 && !rx[12])) || rx[0]!=12 || rx[1]!=9 ||
           memcmp(rx+2,c->uid,8) || rx[10] || rx[11])return;
        r->completed=1;r->status=ADV_VERIFY_FAILED;
        if(!felica_read(bus,c,q->address,r->data,r) || memcmp(r->data,q->data,16))return;
    }
    r->length=16;r->completed=1;r->status=ADV_OK;
}
static bool iso_read(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,uint8_t block,uint8_t out[4],advanced_result_t *r){
    uint8_t cmd[11]={0x22,0x20},rx[255];size_t n;for(unsigned x=0;x<8;x++)cmd[2+x]=c->uid[7-x];cmd[10]=block;
    if(!frame(bus,cmd,11,rx,&n,r) || n!=5 || rx[0]&1)return false;
    memcpy(out,rx+1,4);return true;
}
static void iso15693(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,const advanced_request_t *q,advanced_result_t *r){
    if(q->operation==ADV_SCAN){
        r->status=STATUS_CARD_MISMATCH;
        for(unsigned x=0;x<q->count;x++){
            if(!iso_read(bus,c,q->address+x,r->data+x*4,r))return;
            r->completed++;r->length+=4;r->status=ADV_RF_FAILED;
        }
        r->status=ADV_OK;return;
    }
    r->status=STATUS_CARD_MISMATCH;if(!iso_read(bus,c,q->address,r->data,r))return;
    r->status=ADV_RF_FAILED;
    if(q->operation==ADV_WRITE){
        uint8_t cmd[15]={0x22,0x21},rx[255];size_t n;for(unsigned x=0;x<8;x++)cmd[2+x]=c->uid[7-x];
        cmd[10]=q->address;memcpy(cmd+11,q->data,4);
        if(!frame(bus,cmd,15,rx,&n,r) || n!=1 || rx[0]&1)return;
        r->completed=1;r->status=ADV_VERIFY_FAILED;
        if(!iso_read(bus,c,q->address,r->data,r) || memcmp(r->data,q->data,4))return;
    }
    r->length=4;r->completed=1;r->status=ADV_OK;
}
void Advanced_Execute(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,const advanced_target_t *t,const advanced_request_t *q,advanced_result_t *r){
    memset(r,0,sizeof(*r));r->id=q->id;r->target=q->target;r->operation=q->operation;r->address=q->address;
    r->status=Advanced_Validate(t,q);if(r->status)return;
    if(c->protocol!=t->protocol || c->uid_length!=t->uid_len || memcmp(c->uid,t->uid,t->uid_len)){
        r->status=STATUS_CARD_MISMATCH;return;
    }
    if(c->rf_interface!=(c->protocol==0x80?0x80:1)){
        r->status=STATUS_CARD_MISMATCH;return;
    }
    if(c->protocol==0x80 && (!c->sak_valid ||
       (t->subtype==ADV_CLASSIC_1K && c->sak!=8) || (t->subtype==ADV_CLASSIC_4K && c->sak!=0x18) ||
       (t->subtype==ADV_CLASSIC_MINI && c->sak!=9))){r->status=STATUS_CARD_MISMATCH;return;}
    if(c->protocol==3 && (!c->pmm_valid || (c->pmm[1]!=0xF1 && c->pmm[1]!=0xF2))){r->status=STATUS_CARD_MISMATCH;return;}
    switch(t->protocol){
    case 0x80:classic(bus,c,q,r);break;
    case 2:type2(bus,t,q,r);break;
    case 3:felica(bus,c,q,r);break;
    case 6:iso15693(bus,c,q,r);break;
    default:r->status=ADV_PROTECTED;
    }
}
