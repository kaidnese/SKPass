#include "card_emulation.h"
#include "ce_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static card_emu_t engine;
static uint8_t reply[260];
static size_t apdu(const uint8_t *p,size_t n,uint16_t sw,bool *changed){
    size_t count=CardEmu_Process_Apdu(&engine,p,n,reply,sizeof(reply),changed);
    assert(count>=2 && reply[count-2]==(sw>>8) && reply[count-1]==(sw&255));return count;
}
int main(void){
    ce_request_t q={0},decoded;q.id=0x12345678;q.action=CE_APPLY;
    memcpy(q.config.uid,"\x12\x34\x56\x78",4);q.config.fallback[0]=0x6A;q.config.fallback[1]=0x82;q.config.write_enabled=true;
    q.config.ndef_len=CardEmu_Build_Ndef(0,"https://example.com",q.config.ndef,240);
    uint8_t wire[512];size_t count=CE_Encode_Request(&q,wire,512);assert(count && CE_Decode_Request(wire,count,&decoded));
    assert(!memcmp(&q.config,&decoded.config,sizeof(q.config)));
    for(size_t i=0;i<count;i++)assert(!CE_Decode_Request(wire,i,&decoded));
    wire[count]=0;assert(!CE_Decode_Request(wire,count+1,&decoded));
    CardEmu_Init(&engine,&q.config);
    const uint8_t select[]={0,0xA4,4,0,7,0xD2,0x76,0,0,0x85,1,1,0};
    const uint8_t cc[]={0,0xA4,0,0x0C,2,0xE1,3};
    const uint8_t file[]={0,0xA4,0,0x0C,2,0xE1,4};
    const uint8_t read[]={0,0xB0,0,0,15};
    apdu(read,sizeof(read),0x6A82,NULL);apdu(select,sizeof(select),0x9000,NULL);
    apdu(cc,sizeof(cc),0x9000,NULL);assert(apdu(read,sizeof(read),0x9000,NULL)==17 && reply[9]==0xE1 && reply[10]==4 && reply[14]==0);
    apdu(file,sizeof(file),0x9000,NULL);
    const uint8_t readLen[]={0,0xB0,0,0,2};apdu(readLen,sizeof(readLen),0x9000,NULL);assert(reply[1]==q.config.ndef_len);
    uint8_t text[240];size_t textLen=CardEmu_Build_Ndef(1,"hello NFC",text,240);
    uint8_t write[245]={0,0xD6,0,0,2,0,0};bool changed=false;
    apdu(write,7,0x9000,&changed);assert(changed && !engine.config.ndef_len);
    write[3]=2;write[4]=textLen;memcpy(write+5,text,textLen);apdu(write,5+textLen,0x9000,&changed);assert(!changed);
    write[3]=0;write[4]=2;write[5]=0;write[6]=textLen;apdu(write,7,0x9000,&changed);assert(changed && engine.config.ndef_len==textLen && !memcmp(engine.config.ndef,text,textLen));
    const uint8_t invalid[]={0,0xD6,0,0xFF,1,0};apdu(invalid,sizeof(invalid),0x6B00,NULL);
    engine.config.write_enabled=false;apdu(write,7,0x6982,NULL);CardEmu_Reset_Session(&engine);apdu(write,7,0x6A82,NULL);
    engine.config.rule_count=1;engine.config.rules[0]=(card_emu_rule_t){.match={0x80,0xCA},.match_len=2,.response={1,2,0x90,0},.response_len=4,.prefix=true};
    const uint8_t custom[]={0x80,0xCA,0,0,0};assert(apdu(custom,sizeof(custom),0x9000,NULL)==4 && reply[0]==1);
    for(size_t i=0;i<sizeof(select);i++)CardEmu_Process_Apdu(&engine,select,i,reply,sizeof(reply),NULL);
    assert(!CardEmu_Is_Ndef((const uint8_t *)"\xD1\x01\xFF\x54",4));
    puts("T4T NDEF, bounds, update commit and CE protocol passed");return 0;
}
