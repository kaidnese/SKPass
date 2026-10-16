/* Test the actual production probe with scripted card responses. No RF write
 * commands are accepted by the mock, including under malformed responses. */
#include <stdlib.h>
#include "../main/smartcard_probe.c"

#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
enum { TUNION, EMV, JAPAN, OCTOPUS, TYPE2, TYPE5, TIMEOUT, UNKNOWN, MALFORMED };
static int mode;
static unsigned calls;
static TickType_t ticks;
static const uint8_t idm[]={1,2,3,4,5,6,7,8};
TickType_t xTaskGetTickCount(void){return ticks;}
void vTaskDelay(TickType_t gap){CHECK(gap>0);ticks+=gap;}
static void response(uint8_t *rx,size_t cap,size_t *len,const uint8_t *data,size_t n) {
    CHECK(n<=cap);memcpy(rx,data,n);*len=n;
}
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t bus,const uint8_t *tx,size_t n,
                              uint8_t *rx,size_t cap,size_t *len) {
    CHECK(bus && n);++calls;*len=0;
    if(mode==TIMEOUT){ticks+=150;return ESP_ERR_TIMEOUT;}
    if(mode==TUNION || mode==EMV || mode==UNKNOWN || mode==MALFORMED) {
        CHECK(n>=5 && (tx[1]==0xA4 || tx[1]==0xB0 || tx[1]==0xB2 || tx[1]==0x5C || tx[1]==0xC0));
        const uint8_t ok[]={0x90,0},missing[]={0x6A,0x82},end[]={0x6A,0x83};
        if(tx[1]==0xA4) {
            if(mode==TUNION){response(rx,cap,len,ok,2);return ESP_OK;}
            if((mode==EMV || mode==MALFORMED) && n==20 && tx[5]=='2') {
                const uint8_t ppse[]={0x6F,0x0C,0xA5,0x0A,0x61,8,0x4F,6,0xA0,0,0,3,0x33,1,0x90,0};
                response(rx,cap,len,ppse,sizeof(ppse));return ESP_OK;
            }
            if((mode==EMV || mode==MALFORMED) && tx[4]==6) {response(rx,cap,len,ok,2);return ESP_OK;}
            response(rx,cap,len,missing,2);return ESP_OK;
        }
        if(mode==TUNION && tx[1]==0xB0) {
            uint8_t info[32]={0};const uint8_t number[]={0x01,0x23,0x45,0x67,0x89,0x01,0x23,0x45,0x67,0x89};
            memcpy(info+10,number,10);const uint8_t dates[]={0x20,0x26,0x10,0x13,0x20,0x30,0x10,0x13};memcpy(info+20,dates,8);
            info[30]=0x90;response(rx,cap,len,info,32);return ESP_OK;
        }
        if(mode==TUNION && tx[1]==0x5C){const uint8_t bal[]={0,0,0x30,0x39,0x90,0};response(rx,cap,len,bal,6);return ESP_OK;}
        if(mode==TUNION && tx[1]==0xB2 && tx[2]==1) {
            uint8_t history[25]={0};history[8]=100;history[9]=6;
            const uint8_t date[]={0x20,0x26,0x10,0x13,0x12,0x34,0x56};memcpy(history+16,date,7);history[23]=0x90;
            response(rx,cap,len,history,25);return ESP_OK;
        }
        if(mode==EMV && tx[1]==0xB2 && tx[2]==1) {
            const uint8_t record[]={0x70,0x12,0x5A,8,0x62,0x22,0x02,0x12,0x34,0x56,0x78,0x90,0x5F,0x24,3,0x30,0x10,0x13,0x50,0,0x90,0};
            response(rx,cap,len,record,sizeof(record));return ESP_OK;
        }
        if(mode==MALFORMED && tx[1]==0xB2 && tx[2]==1) {
            const uint8_t invalid[]={0x70,0x81,0x7F,0x5A,2,0x12,0x34,0x90,0};response(rx,cap,len,invalid,sizeof(invalid));return ESP_OK;
        }
        response(rx,cap,len,end,2);return ESP_OK;
    }
    if(mode==JAPAN || mode==OCTOPUS) {
        CHECK(n>=10 && (tx[1]==0x0C || tx[1]==6));
        uint8_t f[30]={0};memcpy(f+2,idm,8);
        if(tx[1]==0x0C) {f[0]=13;f[1]=0x0D;f[10]=1;f[11]=mode==JAPAN?0:0x80;f[12]=mode==JAPAN?3:8;response(rx,cap,len,f,14);return ESP_OK;}
        CHECK(n==16 && tx[10]==1 && tx[13]==1 && tx[14]==0x80);
        if(mode==JAPAN)CHECK(tx[11]==0x0F && tx[12]==9);else CHECK(tx[11]==0x17 && tx[12]==1);
        f[0]=29;f[1]=7;f[12]=1;
        if(mode==JAPAN) {
            if(tx[15]>0){f[0]=12;f[10]=1;response(rx,cap,len,f,13);return ESP_OK;}
            unsigned date=(26u<<9)|(10u<<5)|13u;f[17]=date>>8;f[18]=date;
            f[23]=0xD2;f[24]=4;f[14]=1;
        } else {f[15]=3;f[16]=0xE8;}
        response(rx,cap,len,f,30);return ESP_OK;
    }
    if(mode==TYPE2) {
        CHECK((n==2 && tx[0]==0x30 && tx[1]==3) || (n==1 && tx[0]==0x60));
        uint8_t data[17]={0xE1,0x10,0x12,0};
        if(tx[0]==0x30)response(rx,cap,len,data,17);
        else {const uint8_t version[]={0,4,4,2,1,0,0x0F,3,0};response(rx,cap,len,version,9);}return ESP_OK;
    }
    if(mode==TYPE5) {
        CHECK(n==10 && tx[0]==0x22 && tx[1]==0x2B);
        uint8_t data[13]={0,4};memcpy(data+2,tx+2,8);data[10]=63;data[11]=3;response(rx,cap,len,data,13);return ESP_OK;
    }
    CHECK(false);return ESP_FAIL;
}
static smartcard_info_t probe(int m,uint8_t protocol,uint8_t interface) {
    mode=m;calls=0;ticks=0;pn7150_card_info_t card={.protocol=protocol,.rf_interface=interface,.uid_length=8};
    memcpy(card.uid,idm,8);card.system_code_valid=true;card.system_code[0]=m==OCTOPUS?0x80:0;card.system_code[1]=m==OCTOPUS?8:3;
    smartcard_info_t info;SmartCard_Probe((void *)1,&card,&info);return info;
}
int main(void) {
    smartcard_info_t i=probe(TUNION,4,2);CHECK(!strcmp(i.kind,"transit_tunion") && !strcmp(i.number,"1234567890123456789"));
    CHECK(i.balance==12345 && i.records==1 && i.history[0].amount==100 && !strcmp(i.expiry,"20301013"));
    i=probe(EMV,4,2);CHECK(!strcmp(i.kind,"emv") && !strcmp(i.number,"6222021234567890") && !strcmp(i.expiry,"20301013"));
    i=probe(MALFORMED,4,2);CHECK(!i.number[0]);
    i=probe(JAPAN,3,1);CHECK(!strcmp(i.kind,"transit_japan") && i.balance==1234 && i.records==1 && !strcmp(i.history[0].date,"20261013"));
    i=probe(OCTOPUS,3,1);CHECK(!strcmp(i.kind,"transit_octopus") && i.balance==6500 && i.estimated && i.raw_balance==1000);
    i=probe(TYPE2,2,1);CHECK(!strcmp(i.kind,"ntag213") && i.pages==45 && i.first_page==4 && i.last_page==39);
    i=probe(TYPE5,6,1);CHECK(i.blocks==64 && i.block_size==4);
    i=probe(TIMEOUT,4,2);CHECK(i.session_failed && calls==1);
    i=probe(UNKNOWN,4,2);CHECK(!i.recognized && !i.session_failed && calls<10);
    pn7150_card_info_t card={.protocol=0x80,.uid_length=4,.sak=8,.sak_valid=true};SmartCard_Describe(&card,&i);
    CHECK(i.blocks==64 && i.sectors==16 && i.block_size==16);
    i.records=10;char out[513];for(unsigned part=0;part<=10;part++){size_t size=SmartCard_Encode_Part(&i,part,out,sizeof(out));CHECK(size && size<=512 && out[size-1]=='}');}
    CHECK(!SmartCard_Encode_Part(&i,11,out,sizeof(out)));CHECK(!SmartCard_Encode_Part(&i,0,out,8));
    // Actual longest transport summary stays within the existing wire limit.
    i=probe(TUNION,4,2);strcpy(i.warning,"probe_budget_exhausted");
    CHECK(SmartCard_Encode_Part(&i,0,out,sizeof(out))<=512);
    smartcard_info_t scratch={0};probe_t budget={(void *)1,&scratch,40,0};uint8_t rx[32];size_t length;
    const uint8_t command[]={0,0xA4,4,0,0};unsigned before=calls;
    CHECK(exchange(&budget,command,sizeof(command),rx,sizeof(rx),&length)==ESP_ERR_TIMEOUT && calls==before);
    puts("Smartcard production probes and bounded JSON vectors passed");return 0;
}
