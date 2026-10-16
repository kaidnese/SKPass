#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/skpass_cdc.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct mock_queue {size_t size;unsigned capacity,count;unsigned char *data;};
QueueHandle_t xQueueCreate(unsigned capacity,size_t size){QueueHandle_t q=calloc(1,sizeof(*q));q->size=size;q->capacity=capacity;q->data=calloc(capacity,size);return q;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned wait){(void)wait;if(q->count==q->capacity)return 0;memcpy(q->data+q->count++*q->size,p,q->size);return 1;}
int xQueuePeek(QueueHandle_t q,void *p,unsigned wait){(void)wait;if(!q->count)return 0;memcpy(p,q->data,q->size);return 1;}
int xQueueReceive(QueueHandle_t q,void *p,unsigned wait){if(!xQueuePeek(q,p,wait))return 0;--q->count;memmove(q->data,q->data+q->size,q->count*q->size);return 1;}
int xQueueOverwrite(QueueHandle_t q,const void *p){q->count=1;memcpy(q->data,p,q->size);return 1;}
void xQueueReset(QueueHandle_t q){q->count=0;}
static int64_t now=100000;static bool mounted=true,cdc=true;static unsigned probes,executions;
void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
int64_t esp_timer_get_time(void){return now;}
bool tud_mounted(void){return mounted;}bool tud_cdc_connected(void){return cdc;}
unsigned tud_cdc_available(void){return 0;}unsigned tud_cdc_read(void *p,unsigned n){(void)p;(void)n;return 0;}
unsigned tud_cdc_write(const void *p,unsigned n){(void)p;return n;}void tud_cdc_write_flush(void){}void tud_cdc_write_clear(void){}
bool tud_hid_n_ready(unsigned n){(void)n;return true;}bool tud_hid_n_report(unsigned a,unsigned b,const void *p,unsigned n){(void)a;(void)b;(void)p;(void)n;return true;}
void vTaskDelay(TickType_t n){(void)n;}int xTaskCreate(void (*f)(void *),const char *name,unsigned stack,void *p,unsigned priority,void *handle){(void)f;(void)name;(void)stack;(void)p;(void)priority;(void)handle;return 1;}
esp_err_t tusb_cdc_acm_init(const tinyusb_config_cdcacm_t *c){(void)c;return ESP_OK;}
const esp_app_desc_t *esp_app_get_description(void){static esp_app_desc_t d={.project_name="SKPass",.version="2.9.0"};return &d;}
cJSON *cJSON_CreateObject(void){return NULL;}bool cJSON_AddNumberToObject(cJSON *o,const char *k,double v){(void)o;(void)k;(void)v;return false;}
bool cJSON_AddStringToObject(cJSON *o,const char *k,const char *v){(void)o;(void)k;(void)v;return false;}
bool cJSON_PrintPreallocated(cJSON *o,char *p,int n,bool f){(void)o;(void)p;(void)n;(void)f;return false;}void cJSON_Delete(cJSON *o){(void)o;}
esp_err_t Uf2_Update_Enter(void){return ESP_OK;}bool Uf2_Update_Is_Requested(void){return false;}
esp_err_t Rom_Download_Enter(void){return ESP_OK;}bool Rom_Download_Is_Requested(void){return false;}
size_t Aime_Encode_Event(const aime_card_event_t *e,uint8_t *out){(void)e;(void)out;return 0;}
size_t SmartCard_Encode_Part(const smartcard_info_t *i,unsigned p,char *o,size_t n){(void)i;(void)p;(void)o;(void)n;return 0;}
void Advanced_Describe(const pn7150_card_info_t *c,const smartcard_info_t *i,advanced_target_t *t){(void)i;*t=(advanced_target_t){.uid_len=c->uid_length,.protocol=c->protocol,.subtype=ADV_CLASSIC_1K,.unit=16,.last=63,.available=1};memcpy(t->uid,c->uid,c->uid_length);}
void SmartCard_Probe(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,smartcard_info_t *i){(void)bus;(void)c;memset(i,0,sizeof(*i));++probes;now+=3000000;s_hid_keepalive=now/1000;advanced_timeout();CHECK(s_advanced_outstanding);}
void Advanced_Execute(i2c_master_bus_handle_t bus,const pn7150_card_info_t *c,const advanced_target_t *t,const advanced_request_t *q,advanced_result_t *r){(void)bus;(void)c;++executions;r->status=Advanced_Validate(t,q);}
static void put16t(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void put32t(uint8_t *p,unsigned v){put16t(p,v);put16t(p+2,v>>16);}
static void submit(unsigned id,bool hid){uint8_t p[36]={0};put32t(p,id);put32t(p+4,s_target.token);p[8]=s_target.uid_len;memcpy(p+9,s_target.uid,10);p[19]=s_target.protocol;p[20]=s_target.subtype;p[21]=ADV_READ;put16t(p+23,1);put16t(p+25,1);p[27]=0x60;memset(p+28,0xFF,6);host_frame(0x22,p,sizeof(p),hid?(void *)1:NULL);}
static void submit_scan(unsigned id,bool hid){uint8_t p[36]={0};put32t(p,id);put32t(p+4,s_target.token);p[8]=s_target.uid_len;memcpy(p+9,s_target.uid,10);p[19]=s_target.protocol;p[20]=s_target.subtype;p[21]=ADV_SCAN;put16t(p+25,4);p[27]=0x60;memset(p+28,0xFF,6);host_frame(0x22,p,sizeof(p),hid?(void *)1:NULL);}
static unsigned reply(bool hid){cdc_frame_t f;CHECK(xQueueReceive(hid?s_hid_replies:s_replies,&f,0));CHECK(f.bytes[3]==0x92);return f.bytes[6+22];}
int main(void){
    CHECK(SKPass_CDC_Init()==ESP_OK);pn7150_card_info_t card={.protocol=0x80,.uid_length=4,.uid={1,2,3,4}};smartcard_info_t info={0};
    for(unsigned hid=0;hid<2;hid++){
        s_hid_active=true;s_hid_keepalive=now/1000;SKPass_CDC_Advanced_Card(NULL,&card,&info);
        submit(10+hid*10,hid);CHECK(reply(hid)==ADV_ACCEPTED);SKPass_CDC_Advanced_Run(NULL,&card);CHECK(reply(hid)==ADV_OK);CHECK(!s_advanced_outstanding && !s_target.available);
        s_hid_keepalive=now/1000;SKPass_CDC_Advanced_Card(NULL,&card,&info);submit(11+hid*10,hid);CHECK(reply(hid)==ADV_ACCEPTED);
        unsigned before=executions;pn7150_card_info_t other=card;other.uid[0]^=1;SKPass_CDC_Advanced_Run(NULL,&other);CHECK(reply(hid)==STATUS_CARD_MISMATCH && executions==before);
        s_hid_keepalive=now/1000;SKPass_CDC_Advanced_Card(NULL,&card,&info);submit(12+hid*10,hid);CHECK(reply(hid)==ADV_ACCEPTED);now+=2600000;s_hid_keepalive=now/1000;advanced_timeout();CHECK(reply(hid)==STATUS_CARD_MISMATCH && !s_advanced_outstanding);
        SKPass_CDC_Advanced_Card(NULL,&card,&info);submit(13+hid*10,hid);CHECK(reply(hid)==ADV_ACCEPTED);if(hid)++s_hid_generation;else ++s_cdc_generation;SKPass_CDC_Advanced_Run(NULL,&card);CHECK(reply(hid)==ADV_DISCONNECTED && executions==before);
        SKPass_CDC_Advanced_Card(NULL,&card,&info);submit(14+hid*10,hid);CHECK(reply(hid)==ADV_ACCEPTED);submit(15+hid*10,hid);CHECK(reply(hid)==ADV_BUSY);now+=2600000;s_hid_keepalive=now/1000;advanced_timeout();CHECK(reply(hid)==STATUS_CARD_MISMATCH);
        SKPass_CDC_Advanced_Card(NULL,&card,&info);submit_scan(16+hid*10,hid);CHECK(reply(hid)==ADV_ACCEPTED);SKPass_CDC_Advanced_Run(NULL,&card);CHECK(reply(hid)==ADV_OK && !s_target.available);
    }
    CHECK(probes==4 && executions==4);
    for(unsigned hid=0;hid<2;hid++){
        s_hid_keepalive=now/1000;s_hid_active=true;
        ce_request_t q={.id=100+hid,.action=CE_APPLY};q.config.fallback[0]=0x6A;q.config.fallback[1]=0x82;
        memcpy(q.config.uid,"\x01\x02\x03\x04",4);uint8_t p[512];size_t size=CE_Encode_Request(&q,p,sizeof(p));
        host_frame(CE_REQUEST_MSG,p,size,hid?(void *)1:NULL);CHECK(SKPass_CDC_CE_Pending());
        cdc_frame_t frame;CHECK(xQueueReceive(hid?s_hid_replies:s_replies,&frame,0) && frame.bytes[3]==CE_RESPONSE_MSG && frame.bytes[10]==CE_ACCEPTED && frame.bytes[11]==CE_MODE_PENDING);
        ce_request_t taken;CHECK(SKPass_CDC_CE_Take(&taken) && taken.id==q.id && taken.hid==hid);
        SKPass_CDC_CE_Result(q.id,CE_OK,CE_MODE_LISTEN,q.config.uid,0);
        CHECK(xQueueReceive(hid?s_hid_replies:s_replies,&frame,0) && frame.bytes[10]==CE_OK && frame.bytes[11]==CE_MODE_LISTEN && !SKPass_CDC_CE_Pending());
        const uint8_t apdu[]={0,0xB0,0,0,2};SKPass_CDC_CE_Event(CE_EVT_RX,apdu,sizeof(apdu));
        CHECK(xQueueReceive(hid?s_hid_ce_events:s_ce_events,&frame,0) && frame.bytes[3]==CE_EVENT_MSG && frame.bytes[7]==CE_EVT_RX);
        submit(120+hid,hid);CHECK(reply(hid)==ADV_BUSY);
        SKPass_CDC_CE_Result(0,CE_OK,CE_MODE_RW,NULL,0);CHECK(xQueueReceive(hid?s_hid_replies:s_replies,&frame,0));
        host_frame(CE_REQUEST_MSG,p,size,hid?(void *)1:NULL);CHECK(xQueueReceive(hid?s_hid_replies:s_replies,&frame,0));
        if(hid)++s_hid_generation;else ++s_cdc_generation;
        CHECK(!SKPass_CDC_CE_Take(&taken) && !SKPass_CDC_CE_Pending());
        CHECK(xQueueReceive(hid?s_hid_replies:s_replies,&frame,0) && frame.bytes[10]==CE_DISCONNECTED);
    }
    puts("CDC/HID typed reads, CE dispatch/events, mode exclusion and disconnected requests passed");return 0;
}
