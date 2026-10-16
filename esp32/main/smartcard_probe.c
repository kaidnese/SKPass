#include "smartcard_probe.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* Wire formats checked against nfcpy and Metrodroid; see docs/nfc.md.
 * Only bounded public SELECT / READ / Check / information commands. */
typedef struct {
    i2c_master_bus_handle_t bus;
    smartcard_info_t *info;
    unsigned exchanges;
    TickType_t started;
} probe_t;
static uint32_t s_scan;
static void hex(const uint8_t *p, size_t n, char *out) {
    static const char digits[]="0123456789ABCDEF";
    for(size_t i=0;i<n;i++){out[2*i]=digits[p[i]>>4];out[2*i+1]=digits[p[i]&15];}
    out[n*2]=0;
}
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static bool bcd(const uint8_t *p,size_t n,char *out,bool pad) {
    size_t len=0;
    for(size_t i=0;i<n;i++)for(unsigned j=0;j<2;j++) {
        unsigned d=j ? p[i]&15 : p[i]>>4;
        if(d>9) { if(pad && d==15 && i==n-1 && j==1) {out[len]=0;return len>0;} out[0]=0;return false; }
        out[len++]=(char)('0'+d);
    }
    out[len]=0;return true;
}
static bool date_valid(const char *d) {
    unsigned year=0,month=0,day=0;
    if(strlen(d)<8 || sscanf(d,"%4u%2u%2u",&year,&month,&day)!=3 || year<1990 || year>2199 || !month || month>12) return false;
    static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned max=days[month-1]+(month==2 && year%4==0 && (year%100!=0 || year%400==0));
    return day>0 && day<=max;
}
static void date_bcd(const uint8_t *p,char out[9]) {
    if(!bcd(p,4,out,false) || !date_valid(out))out[0]=0;
}
static esp_err_t exchange(probe_t *p,const uint8_t *tx,size_t n,uint8_t *rx,size_t cap,size_t *len) {
    *len=0;
    if(p->info->session_failed || p->exchanges>=40 ||
       xTaskGetTickCount()-p->started>=pdMS_TO_TICKS(6500)) {
        strcpy(p->info->warning,"probe_budget_exhausted");return ESP_ERR_TIMEOUT;
    }
    ++p->exchanges;
    TickType_t gap=pdMS_TO_TICKS(5)+1;
    vTaskDelay(gap);
    esp_err_t ret=Pn7150_Tag_Transceive(p->bus,tx,n,rx,cap,len);
    if(ret!=ESP_OK) {
        p->info->session_failed=true;
        strcpy(p->info->warning,"rf_session_failed");
    }
    return ret;
}

/* ISO-DEP interface returns APDU bytes (SW1/SW2), not a Frame status byte.
 * Support one 6C correction and bounded 61 GET RESPONSE, no mutating APDU. */
static bool apdu(probe_t *p,const uint8_t *tx,size_t n,uint8_t *data,size_t *size) {
    uint8_t cmd[32],rx[512];size_t got=0,total=0;
    if(n>sizeof(cmd))return false;
    memcpy(cmd,tx,n);*size=0;
    for(unsigned tries=0;tries<4;tries++) {
        if(exchange(p,cmd,n,rx,sizeof(rx),&got)!=ESP_OK || got<2)return false;
        uint8_t sw1=rx[got-2],sw2=rx[got-1];p->info->sw=(uint16_t)sw1<<8|sw2;
        if(sw1==0x6C && tries==0) {
            /* Case 2/4 already has Le; case 3 must append it, not replace data. */
            if(n==5 || (n>=6 && n==(size_t)cmd[4]+6))cmd[n-1]=sw2;
            else if(n>=5 && n==(size_t)cmd[4]+5 && n<sizeof(cmd))cmd[n++]=sw2;
            else return false;
            continue;
        }
        if(total+got-2>480){strcpy(p->info->warning,"apdu_response_too_long");return false;}
        memcpy(data+total,rx,got-2);total+=got-2;
        if(sw1==0x61) {const uint8_t next[]={0x00,0xC0,0,0,sw2};memcpy(cmd,next,5);n=5;continue;}
        if(sw1!=0x90 || sw2!=0)return false;
        *size=total;return true;
    }
    strcpy(p->info->warning,"apdu_response_too_long");return false;
}

/* Strict bounded BER-TLV traversal. Malformed lengths never become PAN/AID. */
static int tlv_find(const uint8_t *p,size_t n,unsigned wanted,const uint8_t **v,size_t *len,unsigned depth) {
    if(depth>8)return -1;
    while(n) {
        unsigned tag=*p++;bool constructed=(tag&0x20)!=0;n--;
        if((tag&31)==31) {
            unsigned count=0;uint8_t b;
            do {if(!n || ++count>2)return -1;b=*p++;n--;tag=(tag<<8)|b;}while(b&0x80);
        }
        if(!n)return -1;
        size_t length=*p++;n--;
        if(length&0x80) {size_t bytes=length&0x7F;length=0;if(!bytes || bytes>2 || bytes>n)return -1;
            while(bytes--){length=(length<<8)|*p++;n--;}}
        if(length>n)return -1;
        if(tag==wanted){*v=p;*len=length;return 1;}
        if(constructed){int r=tlv_find(p,length,wanted,v,len,depth+1);if(r)return r;}
        p+=length;n-=length;
    }
    return 0;
}
static bool select_aid(probe_t *p,const uint8_t *aid,size_t n,uint8_t *out,size_t *size) {
    uint8_t cmd[22]={0,0xA4,4,0};if(n<5 || n>16)return false;
    cmd[4]=(uint8_t)n;memcpy(cmd+5,aid,n);cmd[5+n]=0;
    return apdu(p,cmd,n+6,out,size);
}
static void emv_fields(smartcard_info_t *i,const uint8_t *data,size_t n) {
    const uint8_t *v;size_t len;
    if(tlv_find(data,n,0x5A,&v,&len,0)==1 && len>=6 && len<=10) {
        char pan[21];if(bcd(v,len,pan,true) && strlen(pan)<=19)strcpy(i->number,pan);
    }
    if(tlv_find(data,n,0x5F24,&v,&len,0)==1 && len==3) {
        char d[7];if(bcd(v,3,d,false)){snprintf(i->expiry,sizeof(i->expiry),"20%s",d);if(!date_valid(i->expiry))i->expiry[0]=0;}
    }
    if(tlv_find(data,n,0x50,&v,&len,0)==1) {
        size_t used=len<sizeof(i->label)-1?len:sizeof(i->label)-1;
        for(size_t x=0;x<used;x++)i->label[x]=(v[x]>=32 && v[x]<127)?(char)v[x]:'?';
        i->label[used]=0;
    }
}
static void pboc(probe_t *p,uint8_t *data,size_t *n,bool tunion,bool szt) {
    smartcard_info_t *i=p->info;
    strcpy(i->kind,tunion?"transit_tunion":"transit_pboc");i->recognized=true;strcpy(i->currency,"CNY");
    const uint8_t file[]={0,0xB0,0x95,0,0x1E};
    if(apdu(p,file,sizeof(file),data,n) && *n>=28) {
        hex(data,8,i->issuer);
        /* The documented T-Union 20-digit storage has a leading format nibble;
         * its public card number is the following 19 digits (Metrodroid). */
        if(tunion) {char number[21];if(bcd(data+10,10,number,false))strcpy(i->number,number+1);}
        else if(szt) {
            /* PAY.SZT stores a little-endian binary serial at offset 16,
             * unlike T-Union's BCD number. Report serial, not an assumed print
             * checksum/format; see Metrodroid NewShenzhenTransitData. */
            uint32_t serial=(uint32_t)data[16]|(uint32_t)data[17]<<8|
                            (uint32_t)data[18]<<16|(uint32_t)data[19]<<24;
            if(serial)snprintf(i->number,sizeof(i->number),"%lu",(unsigned long)serial);
        } else if(!bcd(data+12,5,i->number,false))strcpy(i->warning,"unknown_card_number_layout");
        date_bcd(data+20,i->start);date_bcd(data+24,i->expiry);
    } else strcpy(i->warning,"public_file_unavailable");
    const uint8_t bal[]={0x80,0x5C,0,2,4};
    if(apdu(p,bal,sizeof(bal),data,n) && *n==4 && be32(data)<=INT32_MAX){i->balance=(int32_t)be32(data);i->has_balance=true;}
    for(uint8_t r=1;r<=10 && !i->session_failed;r++) {
        uint8_t cmd[]={0,0xB2,r,0xC4,0x17};
        if(!apdu(p,cmd,sizeof(cmd),data,n)) {
            if(i->sw==0x6A83 || i->sw==0x6A82 || i->sw==0x6982 || i->sw==0x6985)break;
            continue;
        }
        if(*n!=23)break;
        smartcard_record_t *h=&i->history[i->records++];hex(data,23,h->raw);
        if(be32(data+5)<=INT32_MAX){h->amount=(int32_t)be32(data+5);h->has_amount=true;}
        h->type=data[9];
        if(bcd(data+16,7,h->date,false)) {
            if(!date_valid(h->date) || data[20]>0x23 || data[21]>0x59 || data[22]>0x59)h->date[0]=0;
        }
    }
}
static void iso_dep(probe_t *p) {
    uint8_t data[480];size_t n=0;smartcard_info_t *i=p->info;
    strcpy(i->kind,"iso_dep");
    const uint8_t union_aid[]={0xA0,0,0,6,0x32,1,1,5};
    if(select_aid(p,union_aid,sizeof(union_aid),data,&n)){pboc(p,data,&n,true,false);return;}
    if(i->session_failed)return;
    /* Legacy single-application Shenzhen Tong: select the payment system
     * directory, then EF 1001 (the public application). */
    const uint8_t pse[]="1PAY.SYS.DDF01";
    if(select_aid(p,pse,sizeof(pse)-1,data,&n)) {
        const uint8_t select[]={0,0xA4,0,0,2,0x10,1};
        if(apdu(p,select,sizeof(select),data,&n)) {
            const uint8_t file[]={0,0xB0,0x95,0,0x1E};
            char start[9],end[9];
            if(apdu(p,file,sizeof(file),data,&n) && n>=28) {
                date_bcd(data+20,start);date_bcd(data+24,end);
                if(start[0] && end[0]){pboc(p,data,&n,false,false);return;}
            }
        }
    }
    if(i->session_failed)return;
    const uint8_t szt[]={'P','A','Y','.','S','Z','T'};
    if(select_aid(p,szt,sizeof(szt),data,&n)){pboc(p,data,&n,false,true);return;}
    if(i->session_failed)return;
    const uint8_t ppse[]="2PAY.SYS.DDF01";
    if(!select_aid(p,ppse,sizeof(ppse)-1,data,&n))return;
    const uint8_t *v;size_t len;
    if(tlv_find(data,n,0x4F,&v,&len,0)!=1 || len<5 || len>16){strcpy(i->warning,"ppse_aid_missing");return;}
    uint8_t aid[16];memcpy(aid,v,len);
    if(!select_aid(p,aid,len,data,&n))return;
    strcpy(i->kind,"emv");i->recognized=true;emv_fields(i,data,n);
    for(uint8_t sfi=1;sfi<=2 && !i->session_failed;sfi++)for(uint8_t record=1;record<=5;record++) {
        uint8_t cmd[]={0,0xB2,record,(uint8_t)((sfi<<3)|4),0};
        if(apdu(p,cmd,sizeof(cmd),data,&n))emv_fields(i,data,n);
        else if(i->session_failed || i->sw==0x6A83 || i->sw==0x6982 || i->sw==0x6985)break;
    }
    if(!i->number[0])strcpy(i->warning,"public_pan_unavailable");
}

/* Frame interface data has an optional trailing PN7150 RF status byte. */
static bool felica_frame(probe_t *p,const uint8_t *cmd,size_t n,uint8_t *rx,size_t *size) {
    if(exchange(p,cmd,n,rx,255,size)!=ESP_OK)return false;
    if(*size<2 || (size_t)rx[0]>*size)return false;
    if(*size==(size_t)rx[0]+1 && rx[*size-1]==0){--*size;return true;}
    return *size==rx[0];
}
static bool felica_poll(probe_t *p,uint16_t system,uint8_t idm[8]) {
    const uint8_t cmd[]={6,0,(uint8_t)(system>>8),(uint8_t)system,1,0};
    uint8_t rx[255];size_t n;
    if(!felica_frame(p,cmd,sizeof(cmd),rx,&n) || n!=20 || rx[1]!=1 ||
       rx[18]!=(uint8_t)(system>>8) || rx[19]!=(uint8_t)system)return false;
    memcpy(idm,rx+2,8);return true;
}
static bool felica_read(probe_t *p,const uint8_t idm[8],uint16_t service,uint8_t block,uint8_t data[16]) {
    uint8_t cmd[16]={16,6};memcpy(cmd+2,idm,8);
    cmd[10]=1;cmd[11]=(uint8_t)service;cmd[12]=(uint8_t)(service>>8);cmd[13]=1;cmd[14]=0x80;cmd[15]=block;
    uint8_t rx[255];size_t n;
    if(!felica_frame(p,cmd,sizeof(cmd),rx,&n) || n<12 || rx[1]!=7 || memcmp(rx+2,idm,8))return false;
    if(rx[10] || rx[11])return false;
    if(n!=29 || rx[12]!=1)return false;
    memcpy(data,rx+13,16);return true;
}
static void felica(probe_t *p,const pn7150_card_info_t *c) {
    smartcard_info_t *i=p->info;strcpy(i->kind,"felica");
    if(c->uid_length!=8)return;
    uint16_t original=c->system_code_valid?(uint16_t)c->system_code[0]<<8|c->system_code[1]:0xFFFF;
    i->arcade=original==0x88B4 || original==0xFE00;
    bool japan=original==3,octopus=original==0x8008,switched=false;
    uint8_t idm[8],data[16];memcpy(idm,c->uid,8);
    if(c->pmm_valid && (c->pmm[1]==0xF1 || c->pmm[1]==0xF2)){
        if(original==0x88B4)i->lite_s_verified=true;
        else if(original==0xFFFF){
            uint8_t lite[8];
            if(felica_poll(p,0x88B4,lite) && !memcmp(lite,c->uid,8)){
                i->lite_s_verified=true;i->arcade=true;strcpy(i->kind,"amusement_ic");
                i->recognized=true;return;
            }
        }
    }
    /* Request System Code lists other systems without changing the selected
     * one. Avoid speculative system Polling when the list is unavailable. */
    uint8_t cmd[10]={10,0x0C};memcpy(cmd+2,c->uid,8);
    uint8_t rx[255];size_t n;
    if(felica_frame(p,cmd,sizeof(cmd),rx,&n) && n>=11 && rx[1]==0x0D &&
       !memcmp(rx+2,c->uid,8) && n==11u+2u*rx[10]) {
        for(unsigned x=0;x<rx[10];x++) {
            uint16_t s=(uint16_t)rx[11+2*x]<<8|rx[12+2*x];
            japan|=s==3;octopus|=s==0x8008;i->arcade|=s==0x88B4 || s==0xFE00;
        }
    }
    if(i->arcade){strcpy(i->kind,"amusement_ic");i->recognized=true;}
    /* An advertised transport system is enough for the category, even if
     * public balance/history reads are refused. Never fall back to an arcade
     * Access Code merely because those transport fields are unavailable. */
    if(octopus){strcpy(i->kind,"transit_octopus");strcpy(i->currency,"HKD");i->recognized=true;}
    else if(japan){strcpy(i->kind,"transit_japan");strcpy(i->currency,"JPY");i->recognized=true;}
    if(i->session_failed)return;
    if(octopus && original!=0x8008) {if(!felica_poll(p,0x8008,idm))return;switched=true;}
    /* Known arcade systems never receive speculative transit-service probes.
     * Read an advertised transport system only after switching to that system. */
    if((octopus || (!japan && !i->arcade)) && felica_read(p,idm,0x0117,0,data)) {
        strcpy(i->kind,"transit_octopus");strcpy(i->currency,"HKD");i->recognized=true;
        i->raw_balance=be32(data);int64_t cents=((int64_t)i->raw_balance-350)*10;
        if(cents>=INT32_MIN && cents<=INT32_MAX){i->balance=(int32_t)cents;i->has_balance=true;}
        i->estimated=true;strcpy(i->warning,"octopus_legacy_formula_estimate");
    } else if(!i->session_failed && !octopus && (japan || !i->arcade)) {
        if(japan && original!=3){if(!felica_poll(p,3,idm))goto restore;switched=true;}
        for(uint8_t b=0;b<5 && !i->session_failed;b++) {
            if(!felica_read(p,idm,0x090F,b,data))break;
            strcpy(i->kind,"transit_japan");strcpy(i->currency,"JPY");i->recognized=true;
            smartcard_record_t *h=&i->history[i->records++];hex(data,16,h->raw);
            h->type=data[1];h->balance=data[10]|(uint16_t)data[11]<<8;h->has_balance=true;
            if(!b){i->balance=h->balance;i->has_balance=true;}
            unsigned packed=(unsigned)data[4]<<8|data[5];
            snprintf(h->date,sizeof(h->date),"%04u%02u%02u",2000+(packed>>9),(packed>>5)&15,packed&31);
            if(!date_valid(h->date))h->date[0]=0;
            if(data[0]!=0xC7 && data[0]!=0xC8){h->entry=(uint16_t)data[6]<<8|data[7];h->exit=(uint16_t)data[8]<<8|data[9];}
        }
    }
restore:
    if(switched && !i->session_failed) {
        uint8_t restored[8];if(original==0xFFFF || !felica_poll(p,original,restored) || memcmp(restored,c->uid,8)) {
            i->session_failed=true;strcpy(i->warning,"felica_restore_failed");
        }
    }
    if(strncmp(i->kind,"transit_",8)==0 && !i->has_balance && !i->records && !i->warning[0])
        strcpy(i->warning,"public_file_unavailable");
}
static bool frame_payload(probe_t *p,const uint8_t *cmd,size_t n,uint8_t *rx,size_t *len) {
    if(exchange(p,cmd,n,rx,255,len)!=ESP_OK || !*len)return false;
    if(rx[*len-1]!=0)return false;
    --*len;return true;
}
static void type2(probe_t *p) {
    smartcard_info_t *i=p->info;strcpy(i->kind,"type2");uint8_t rx[255];size_t n=0;
    /* Older Ultralight chips do not implement GET_VERSION. Read CC first so
     * useful memory information survives an unsupported version exchange. */
    const uint8_t cc[]={0x30,3};
    if(frame_payload(p,cc,2,rx,&n) && n==16 && rx[0]==0xE1 && (rx[1]>>4)==1 && rx[2]) {
        i->first_page=4;i->last_page=(uint16_t)(3+rx[2]*2);i->pages=(uint16_t)(4+rx[2]*2);
        i->t2_writable=(rx[3]&15)==0;
        i->recognized=true;
    }
    if(i->session_failed)return;
    const uint8_t version[]={0x60};
    bool version_ok=frame_payload(p,version,1,rx,&n) && n==8;
    if(version_ok && rx[1]==4 && rx[2]==4 && rx[6]>=0x0F && rx[6]<=0x13) {
        if(rx[6]==0x0F){strcpy(i->kind,"ntag213");i->pages=45;i->last_page=39;}
        if(rx[6]==0x11){strcpy(i->kind,"ntag215");i->pages=135;i->last_page=129;}
        if(rx[6]==0x13){strcpy(i->kind,"ntag216");i->pages=231;i->last_page=225;}
        i->recognized=true;i->first_page=4;
    } else if(version_ok && rx[1]==4 && rx[2]==3){strcpy(i->kind,"ultralight");i->recognized=true;}
    if(!i->recognized)strcpy(i->warning,"cc_or_version_unavailable");
}
static void type5(probe_t *p,const pn7150_card_info_t *c) {
    smartcard_info_t *i=p->info;strcpy(i->kind,"iso15693");
    if(c->uid_length!=8)return;
    uint8_t cmd[10]={0x22,0x2B};for(unsigned x=0;x<8;x++)cmd[2+x]=c->uid[7-x];
    uint8_t rx[255];size_t n;
    if(!frame_payload(p,cmd,sizeof(cmd),rx,&n) || n<10 || rx[0]&1 || memcmp(rx+2,cmd+2,8))return;
    unsigned flags=rx[1];size_t pos=10;
    if(flags&1)++pos;
    if(flags&2)++pos;
    if((flags&4) && pos+2<=n){i->blocks=(uint16_t)rx[pos]+1;i->block_size=(rx[pos+1]&31)+1;i->recognized=true;}
    else strcpy(i->warning,"memory_information_unavailable");
}
void SmartCard_Describe(const pn7150_card_info_t *card,smartcard_info_t *i) {
    if(!i)return;
    memset(i,0,sizeof(*i));if(!card)return;
    if(!++s_scan)++s_scan;
    i->scan=s_scan;hex(card->uid,card->uid_length<=10?card->uid_length:10,i->uid);
    i->protocol=card->protocol;i->rf_interface=card->rf_interface;
    strcpy(i->kind,"unknown");
    if(card->protocol==0x80) {
        strcpy(i->kind,"classic");i->block_size=16;
        if(card->sak_valid) {if(card->sak==8){i->sectors=16;i->blocks=64;}
            else if(card->sak==0x18){i->sectors=40;i->blocks=256;}
            else if(card->sak==9){i->sectors=5;i->blocks=20;}}
        i->recognized=i->blocks!=0;
    } else if(card->protocol==3)strcpy(i->kind,"felica");
    else if(card->protocol==4)strcpy(i->kind,"iso_dep");
    else if(card->protocol==2)strcpy(i->kind,"type2");
    else if(card->protocol==6)strcpy(i->kind,"iso15693");
    if(card->protocol==3 && card->pmm_valid && (card->pmm[1]==0xF1 || card->pmm[1]==0xF2) &&
       card->system_code_valid && card->system_code[0]==0x88 && card->system_code[1]==0xB4)
        i->lite_s_verified=true;
}
void SmartCard_Probe(i2c_master_bus_handle_t bus,const pn7150_card_info_t *card,smartcard_info_t *i) {
    SmartCard_Describe(card,i);if(!i || !bus || !card)return;
    probe_t p={bus,i,0,xTaskGetTickCount()};
    switch(card->protocol) {
    case 0x80:break;
    case 4:if(card->rf_interface==2)iso_dep(&p);break;
    case 3:felica(&p,card);break;
    case 2:type2(&p);break;
    case 6:type5(&p,card);break;
    default:break;
    }
}

typedef struct {char *out;size_t cap,used;bool ok;} json_t;
static void append(json_t *j,const char *fmt,...) {
    if(!j->ok)return;
    va_list args;va_start(args,fmt);
    int n=vsnprintf(j->out+j->used,j->cap-j->used,fmt,args);va_end(args);
    if(n<0 || (size_t)n>=j->cap-j->used){j->ok=false;return;}j->used+=(size_t)n;
}
static void str(json_t *j,const char *key,const char *v) {
    if(!*v)return;
    append(j,",\"%s\":\"",key);
    for(const unsigned char *p=(const unsigned char *)v;*p;p++) {
        if(*p=='"' || *p=='\\')append(j,"\\%c",*p);
        else if(*p<32)append(j,"\\u%04x",*p);else append(j,"%c",*p);
    }
    append(j,"\"");
}
size_t SmartCard_Encode_Part(const smartcard_info_t *i,unsigned part,char *out,size_t cap) {
    if(!i || !out || !cap || i->records>SMARTCARD_MAX_RECORDS || part>i->records)return 0;
    json_t j={out,cap,0,true};
    append(&j,"{\"schema\":1,\"scan\":%lu,\"uid\":\"%s\",\"part\":%u,\"count\":%u",(unsigned long)i->scan,i->uid,part,i->records+1);
    if(!part) {
        str(&j,"kind",i->kind);str(&j,"number",i->number);str(&j,"issuer",i->issuer);str(&j,"start",i->start);
        str(&j,"expiry",i->expiry);str(&j,"label",i->label);str(&j,"currency",i->currency);str(&j,"warning",i->warning);
        append(&j,",\"status\":\"%s\"",i->session_failed?"rf_error":!i->recognized?"unrecognized":i->warning[0]?"partial":"ok");
        append(&j,",\"protocol\":%u,\"interface\":%u",i->protocol,i->rf_interface);
        if(i->has_balance)append(&j,",\"balance\":%ld",(long)i->balance);
        if(i->estimated)append(&j,",\"estimate\":true,\"raw_balance\":%lu",(unsigned long)i->raw_balance);
        if(i->blocks)append(&j,",\"blocks\":%u,\"block_size\":%u,\"sectors\":%u",i->blocks,i->block_size,i->sectors);
        if(i->pages)append(&j,",\"pages\":%u,\"first_page\":%u,\"last_page\":%u",i->pages,i->first_page,i->last_page);
        if(i->sw)append(&j,",\"sw\":\"%04X\"",i->sw);
    } else {
        const smartcard_record_t *h=&i->history[part-1];str(&j,"date",h->date);str(&j,"raw",h->raw);
        append(&j,",\"type\":%u,\"entry\":%u,\"exit\":%u",h->type,h->entry,h->exit);
        if(h->has_amount)append(&j,",\"amount\":%ld",(long)h->amount);
        if(h->has_balance)append(&j,",\"balance\":%ld",(long)h->balance);
    }
    append(&j,"}");return j.ok?j.used:0;
}
