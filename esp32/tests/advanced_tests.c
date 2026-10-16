#include "advanced_reader.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned calls,writes;static uint8_t protocol,storage[256][16],last_auth,last_sector;static int fail_block=-1;
static bool fail,wrong_uid,change_after_write;static pn7150_card_info_t active;
void vTaskDelay(TickType_t n){(void)n;}
esp_err_t Pn7150_ReActivate(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c){(void)bus;(void)c;return wrong_uid?ESP_FAIL:ESP_OK;}
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t bus,const uint8_t *p,size_t size,uint8_t *rx,size_t capacity,size_t *n){
    (void)bus;(void)capacity;++calls;*n=0;if(fail)return ESP_ERR_TIMEOUT;
    static uint8_t write_block;
    if(protocol==0x80){
        if(p[0]==0x40){CHECK(size==9);last_auth=p[2];last_sector=p[1];rx[0]=0x40;rx[1]=0;*n=2;}
        else if(p[0]==0x10 && size==3 && p[1]==0x30){if(p[2]==fail_block)return ESP_ERR_TIMEOUT;rx[0]=0x10;memcpy(rx+1,storage[p[2]],16);rx[17]=0;*n=18;}
        else if(p[0]==0x10 && size==3 && p[1]==0xA0){write_block=p[2];rx[0]=0x10;rx[1]=0;*n=2;}
        else if(p[0]==0x10 && size==17){memcpy(storage[write_block],p+1,16);writes++;rx[0]=0x10;rx[1]=0;*n=2;}
        else CHECK(false);
    }else if(protocol==2){
        if(p[0]==0x30){for(unsigned x=0;x<4;x++)memcpy(rx+4*x,storage[p[1]+x],4);rx[16]=0;*n=17;}
        else if(p[0]==0xA2){CHECK(size==6);memcpy(storage[p[1]],p+2,4);writes++;rx[0]=0x0A;rx[1]=0;*n=2;if(change_after_write)storage[0][0]^=1;}
        else CHECK(false);
    }else if(protocol==3){
        CHECK(size==16 || size==32);CHECK(!memcmp(p+2,active.uid,8));CHECK(p[15]<14);
        rx[1]=p[1]+1;memcpy(rx+2,p+2,8);rx[10]=rx[11]=0;
        if(p[1]==6){CHECK(p[11]==0x0B && p[12]==0);rx[0]=29;rx[12]=1;memcpy(rx+13,storage[p[15]],16);*n=29;}
        else {CHECK(p[1]==8 && p[11]==9 && !p[12]);memcpy(storage[p[15]],p+16,16);writes++;rx[0]=12;*n=12;}
    }else if(protocol==6){
        CHECK(p[0]==0x22);for(unsigned x=0;x<8;x++)CHECK(p[2+x]==active.uid[7-x]);
        rx[0]=0;
        if(p[1]==0x20){memcpy(rx+1,storage[p[10]],4);rx[5]=0;*n=6;}
        else {CHECK(p[1]==0x21 && size==15);memcpy(storage[p[10]],p+11,4);writes++;rx[1]=0;*n=2;}
    }else CHECK(false);
    return ESP_OK;
}
static advanced_request_t request(advanced_target_t t,int op,unsigned block,unsigned count){
    advanced_request_t q={.id=123,.target=t,.operation=op,.address=block,.count=count,.key_type=0x60};
    memset(q.key,0xFF,6);q.length=(op==ADV_READ || op==ADV_SCAN)?0:count*t.unit;memset(q.data,0xAB,q.length);return q;
}
static advanced_target_t describe(uint8_t p,const char *kind){
    memset(&active,0,sizeof(active));active.protocol=p;active.rf_interface=p==0x80?0x80:1;
    active.uid_length=p==0x80?4:p==2?7:8;for(unsigned x=0;x<active.uid_length;x++)active.uid[x]=x+1;
    active.sak=8;active.sak_valid=true;active.pmm_valid=true;active.pmm[1]=0xF1;
    active.system_code_valid=true;active.system_code[0]=0x88;active.system_code[1]=0xB4;
    smartcard_info_t i={.recognized=true,.pages=135,.first_page=4,.last_page=129,.blocks=32,.block_size=4,.lite_s_verified=true,.t2_writable=true};
    strcpy(i.kind,kind);advanced_target_t t;Advanced_Describe(&active,&i,&t);t.token=1;protocol=p;return t;
}
int main(void){
    advanced_result_t r;advanced_target_t t=describe(0x80,"classic");CHECK(t.subtype==ADV_CLASSIC_1K);
    advanced_request_t q=request(t,ADV_WRITE,1,1);q.key_type=0x61;Advanced_Execute((void*)1,&active,&t,&q,&r);
    CHECK(r.status==ADV_OK && writes==1 && last_auth==0x90 && r.length==16);
    unsigned before=calls;q.target.uid[0]^=1;Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==STATUS_CARD_MISMATCH && calls==before);
    q=request(t,ADV_WRITE,0,1);CHECK(Advanced_Validate(&t,&q)==ADV_PROTECTED);
    q=request(t,ADV_WRITE,3,1);CHECK(Advanced_Validate(&t,&q)==ADV_PROTECTED);q.flags=ADV_CONFIRM_TRAILER;CHECK(Advanced_Validate(&t,&q)==ADV_INVALID);
    memset(q.data,0xFF,16);q.data[6]=0xFF;q.data[7]=7;q.data[8]=0x80;CHECK(Advanced_Validate(&t,&q)==ADV_OK);
    Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && last_auth==0x10 && r.length==16);
    q=request(t,ADV_UID_WRITE,0,1);q.length=4;q.flags=ADV_CONFIRM_GEN2|ADV_CONFIRM_UID;uint8_t uid[]={0x11,0x22,0x33,0x44};memcpy(q.data,uid,4);
    memcpy(storage[0],active.uid,4);storage[0][4]=1^2^3^4;storage[0][5]=8;storage[0][6]=4;
    Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && !memcmp(storage[0],uid,4) && storage[0][4]==0x44);
    wrong_uid=true;q=request(t,ADV_READ,1,1);before=calls;Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==STATUS_CARD_MISMATCH && calls==before);wrong_uid=false;
    before=writes;q=request(t,ADV_SCAN,0,4);fail_block=2;Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_RF_FAILED && r.completed==2 && r.length==32 && writes==before);fail_block=-1;
    q=request(t,ADV_SCAN,3,2);CHECK(Advanced_Validate(&t,&q)==ADV_INVALID);
    active.sak=0x18;smartcard_info_t ci={0};Advanced_Describe(&active,&ci,&t);t.token=1;CHECK(t.subtype==ADV_CLASSIC_4K);
    q=request(t,ADV_SCAN,240,16);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.completed==16 && r.length==256 && last_sector==39 && writes==before);
    q=request(t,ADV_SCAN,127,2);CHECK(Advanced_Validate(&t,&q)==ADV_INVALID);
    q=request(t,ADV_WRITE,128,1);CHECK(Advanced_Validate(&t,&q)==ADV_INVALID);
    t=describe(2,"ntag215");CHECK(t.subtype==ADV_NTAG215 && t.last==129);
    memcpy(storage[0],active.uid,3);storage[0][3]=0x88^1^2^3;memcpy(storage[1],active.uid+3,4);storage[2][0]=4^5^6^7;
    q=request(t,ADV_WRITE,129,1);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.length==4);
    q.address=130;before=calls;Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_INVALID && calls==before);
    q=request(t,ADV_READ,128,2);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.length==8);
    before=writes;q=request(t,ADV_SCAN,120,10);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.completed==10 && r.length==40 && writes==before);
    const uint8_t ndef[]={3,9,0xD1,1,5,'T',2,'z','h','h','i',0xFE,0,0,0,0};
    q=request(t,ADV_NDEF,4,4);memcpy(q.data,ndef,sizeof(ndef));Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.completed==4 && r.length==16 && !memcmp(storage[4],ndef,4));
    change_after_write=true;before=writes;Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==STATUS_CARD_MISMATCH && writes==before+1);change_after_write=false;
    t=describe(3,"amusement_ic");CHECK(t.subtype==ADV_LITE_S);
    q=request(t,ADV_WRITE,13,1);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.length==16);
    q.address=14;before=calls;Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_INVALID && calls==before);
    before=writes;q=request(t,ADV_SCAN,0,14);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.length==224 && r.completed==14 && writes==before);
    active.pmm[1]=0xF0;smartcard_info_t info={.lite_s_verified=true};Advanced_Describe(&active,&info,&t);CHECK(!t.subtype);
    t=describe(6,"iso15693");q=request(t,ADV_WRITE,31,1);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.length==4);
    before=writes;q=request(t,ADV_SCAN,0,32);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==ADV_OK && r.length==128 && r.completed==32 && writes==before);
    fail=true;q=request(t,ADV_READ,0,1);Advanced_Execute((void*)1,&active,&t,&q,&r);CHECK(r.status==STATUS_CARD_MISMATCH);fail=false;
    q.target.protocol=3;CHECK(Advanced_Validate(&t,&q)==STATUS_CARD_MISMATCH);
    uint8_t encoded[300];r.length=257;CHECK(!Advanced_Encode_Result(&r,encoded,sizeof(encoded)));
    uint8_t malformed[36]={0};advanced_request_t parsed;CHECK(!Advanced_Decode_Request(malformed,36,&parsed));
    puts("Advanced typed dispatch, safety boundaries, UID/BCC and readback passed");return 0;
}
