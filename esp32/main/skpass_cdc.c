#include "skpass_cdc.h"
#include "skpass_usb.h"
#include "uf2_update.h"
#include "rom_download.h"
#include "reader_frame.h"
#include "hid_wire.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_app_desc.h"
#include "firmware_identity.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
static char s_firmware_id[96];
static portMUX_TYPE s_nfc_lock = portMUX_INITIALIZER_UNLOCKED;
static skpass_nfc_state_t s_nfc_state = SKPASS_NFC_INITIALIZING;
void SKPass_CDC_Set_Nfc_State(skpass_nfc_state_t state)
{
    portENTER_CRITICAL(&s_nfc_lock); s_nfc_state = state; portEXIT_CRITICAL(&s_nfc_lock);
}
static void identify_firmware(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(s_firmware_id, sizeof(s_firmware_id), "%s-%s-retry-rom-adv-scan-ce-uf2-write",
             app->project_name, app->version);
    ESP_LOGI("SKPASS_CDC", "FW %s built=%s %s IDF=%s", s_firmware_id,
             app->date, app->time, app->idf_ver);
}
#include "tusb.h"
#include "tusb_cdc_acm.h"
#if CONFIG_SKPASS_ENABLE_CDC && !CFG_TUD_CDC
#error "SKPASS_ENABLE_CDC requires TinyUSB CDC support"
#endif
typedef struct {uint16_t size; uint8_t bytes[READER_MAX_FRAME];} cdc_frame_t;
static QueueHandle_t s_events, s_debug, s_replies, s_writes, s_details;
static QueueHandle_t s_hid_events, s_hid_debug, s_hid_replies, s_hid_rx, s_hid_details;
static QueueHandle_t s_advanced, s_targets, s_hid_targets;
static QueueHandle_t s_ce_requests, s_ce_events, s_hid_ce_events;
static card_emu_config_t s_ce_config;
static uint8_t s_ce_mode;
static bool s_ce_outstanding, s_ce_hid;
static unsigned s_ce_sequence;
typedef struct {advanced_request_t request;int64_t expires;bool hid;unsigned generation;} pending_advanced_t;
static advanced_target_t s_target;
static int64_t s_target_seen;
static uint32_t s_target_token, s_last_advanced_id;
static bool s_advanced_outstanding;
static unsigned s_cdc_generation;
typedef struct { writer_request_t request; int64_t expires; bool hid; } pending_write_t;
static portMUX_TYPE s_write_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_write_outstanding;
static bool s_write_disconnected;
static bool s_write_hid;
static bool s_hid_active;
static uint32_t s_hid_keepalive;
static unsigned s_hid_generation;
static uint32_t s_hid_session;
static uint32_t s_last_write_id;
static unsigned s_dropped;
static bool s_has_event;
static uint32_t s_last_event;
static bool cdc_connected(void) {
#if CONFIG_SKPASS_ENABLE_CDC
    return tud_cdc_connected();
#else
    return false;
#endif
}
static bool hid_connected(void) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&s_write_lock);
    bool active = s_hid_active && now - s_hid_keepalive < 3000;
    portEXIT_CRITICAL(&s_write_lock);
    return active && tud_mounted();
}
static void close_hid_session(void) {
    portENTER_CRITICAL(&s_write_lock);
    s_hid_active = false;
    ++s_hid_generation;
    if (s_write_outstanding && s_write_hid) s_write_disconnected = true;
    portEXIT_CRITICAL(&s_write_lock);
    xQueueReset(s_hid_events); xQueueReset(s_hid_debug); xQueueReset(s_hid_replies);
    xQueueReset(s_hid_details);
    xQueueReset(s_hid_targets);
    if(s_hid_ce_events)xQueueReset(s_hid_ce_events);
    /* On a new host session advertise the currently leased identity too.
     * RF remains serialized in the NFC task; no callback probes a card. */
}
void SKPass_HID_Receive(const uint8_t report[64]) {
    if (s_hid_rx) xQueueSend(s_hid_rx, report, 0);
}

static size_t device_info(uint8_t out[READER_MAX_PAYLOAD])
{
    const esp_app_desc_t *app = esp_app_get_description();
    char built[40], sha[65];
    snprintf(built, sizeof(built), "%s %s", app->date, app->time);
    SKPass_Format_Firmware_Hash(app->app_elf_sha256, sha);
    portENTER_CRITICAL(&s_nfc_lock);
    skpass_nfc_state_t state = s_nfc_state;
    portEXIT_CRITICAL(&s_nfc_lock);
    static const char *states[] = {"initializing", "ready", "reading", "recovering", "update", "card_emulation"};
    cJSON *info = cJSON_CreateObject();
    if (!info) return 0;
    bool ok = cJSON_AddNumberToObject(info, "schema", 1) &&
        cJSON_AddStringToObject(info, "firmware", s_firmware_id) &&
        cJSON_AddStringToObject(info, "version", app->version) &&
        cJSON_AddStringToObject(info, "built", built) &&
        cJSON_AddStringToObject(info, "idf", app->idf_ver) &&
        cJSON_AddStringToObject(info, "target", CONFIG_IDF_TARGET) &&
        cJSON_AddStringToObject(info, "app_sha256", sha) &&
        cJSON_AddStringToObject(info, "usb_state", "connected") &&
        cJSON_AddStringToObject(info, "nfc_state", states[state]);
    if (ok) ok = cJSON_PrintPreallocated(info, (char *)out, READER_MAX_PAYLOAD, false);
    cJSON_Delete(info);
    return ok ? strlen((char *)out) : 0;
}

#if CONFIG_SKPASS_ENABLE_CDC
static void cdc_line_changed(int interface,cdcacm_event_t *event)
{
    (void)interface;
    if (event->type==CDC_EVENT_LINE_STATE_CHANGED && !event->line_state_changed_data.dtr) {
        portENTER_CRITICAL(&s_write_lock);
        if (s_write_outstanding && !s_write_hid) s_write_disconnected=true;
        ++s_cdc_generation;
        portEXIT_CRITICAL(&s_write_lock);
    }
}

#endif

static void write_reply(const writer_result_t *r, bool hid)
{
    if (!s_replies) return;
    uint8_t payload[WRITER_RESULT_BYTES]; cdc_frame_t frame;
    size_t n=writer_encode_result(r,payload);
    frame.size=reader_encode_frame(0x90,payload,n,frame.bytes,sizeof(frame.bytes));
    if (xQueueSend(hid ? s_hid_replies : s_replies,&frame,0)!=pdTRUE)
        ESP_LOGW("SKPASS_CDC", "Write result queue full; do not auto-retry writes");
}
static writer_result_t request_reply(const writer_request_t *q, uint8_t status)
{
    writer_result_t r={0};r.id=q->id;r.status=status;r.uid_len=q->uid_len;
    memcpy(r.uid,q->uid,10);return r;
}
static void finish_write(void)
{
    portENTER_CRITICAL(&s_write_lock);s_write_outstanding=false;portEXIT_CRITICAL(&s_write_lock);
}
void SKPass_CDC_Write_Result(const writer_result_t *r) {write_reply(r,s_write_hid);finish_write();}
static void advanced_reply(const advanced_result_t *r,bool hid){
    uint8_t payload[ADV_RESULT_HEADER+ADV_MAX_DATA];cdc_frame_t f;
    size_t size=Advanced_Encode_Result(r,payload,sizeof(payload));
    f.size=reader_encode_frame(ADV_RESPONSE_MSG,payload,size,f.bytes,sizeof(f.bytes));
    if(xQueueSend(hid?s_hid_replies:s_replies,&f,0)!=pdTRUE)
        ESP_LOGW("SKPASS_CDC","Advanced result queue full; never retry writes automatically");
}
static void target_reply(const advanced_target_t *t){
    if(!s_targets)return;
    uint8_t p[ADV_TARGET_BYTES];cdc_frame_t f;Advanced_Encode_Target(t,p);
    f.size=reader_encode_frame(ADV_TARGET_MSG,p,sizeof(p),f.bytes,sizeof(f.bytes));
    xQueueOverwrite(s_targets,&f);if(hid_connected())xQueueOverwrite(s_hid_targets,&f);
}
static void refresh_advanced_target(bool hid){
    portENTER_CRITICAL(&s_write_lock);advanced_target_t t=s_target;
    if(esp_timer_get_time()-s_target_seen>1800000)t.available=0;
    portEXIT_CRITICAL(&s_write_lock);
    uint8_t p[ADV_TARGET_BYTES];cdc_frame_t f;Advanced_Encode_Target(&t,p);
    f.size=reader_encode_frame(ADV_TARGET_MSG,p,sizeof(p),f.bytes,sizeof(f.bytes));
    xQueueOverwrite(hid?s_hid_targets:s_targets,&f);
}
void SKPass_CDC_Advanced_Clear(void){
    portENTER_CRITICAL(&s_write_lock);
    s_target.available=0;s_target_seen=0;advanced_target_t target=s_target;
    portEXIT_CRITICAL(&s_write_lock);target_reply(&target);
}
bool SKPass_CDC_Advanced_Pending(void){
    portENTER_CRITICAL(&s_write_lock);bool pending=s_advanced_outstanding;portEXIT_CRITICAL(&s_write_lock);
    return pending;
}
static void advanced_timeout(void){
    int64_t now=esp_timer_get_time();bool clear;
    portENTER_CRITICAL(&s_write_lock);
    clear=s_target.available && now-s_target_seen>1800000;
    if(clear)s_target.available=0;
    advanced_target_t target=s_target;
    portEXIT_CRITICAL(&s_write_lock);if(clear)target_reply(&target);
    pending_advanced_t p;
    if(!s_advanced || xQueuePeek(s_advanced,&p,0)!=pdTRUE)return;
    bool connected=p.hid?hid_connected() && p.generation==s_hid_generation:cdc_connected() && p.generation==s_cdc_generation;
    if(connected && now<p.expires && !clear && target.available)return;
    if(xQueueReceive(s_advanced,&p,0)!=pdTRUE)return;
    advanced_result_t r={.id=p.request.id,.target=p.request.target,.operation=p.request.operation,
        .address=p.request.address,.status=connected?STATUS_CARD_MISMATCH:ADV_DISCONNECTED};
    portENTER_CRITICAL(&s_write_lock);s_advanced_outstanding=false;portEXIT_CRITICAL(&s_write_lock);
    advanced_reply(&r,p.hid);
}
void SKPass_CDC_Advanced_Card(i2c_master_bus_handle_t bus,const pn7150_card_info_t *card,const smartcard_info_t *info){
    (void)bus;
    advanced_target_t target;Advanced_Describe(card,info,&target);
    portENTER_CRITICAL(&s_write_lock);
    bool same=s_target.available && s_target.uid_len==target.uid_len && s_target.protocol==target.protocol &&
        s_target.subtype==target.subtype && !memcmp(s_target.uid,target.uid,target.uid_len);
    if(!same && !++s_target_token)++s_target_token;
    target.token=same?s_target.token:s_target_token;s_target=target;s_target_seen=esp_timer_get_time();
    portEXIT_CRITICAL(&s_write_lock);
    target_reply(&target);
}
void SKPass_CDC_Advanced_Run(i2c_master_bus_handle_t bus,const pn7150_card_info_t *card){
    pending_advanced_t p;
    if(!s_advanced || xQueueReceive(s_advanced,&p,0)!=pdTRUE)return;
    advanced_result_t r={.id=p.request.id,.target=p.request.target,.operation=p.request.operation,.address=p.request.address};
    portENTER_CRITICAL(&s_write_lock);
    advanced_target_t previous=s_target;
    if(esp_timer_get_time()-s_target_seen>1800000)previous.available=0;
    portEXIT_CRITICAL(&s_write_lock);
    bool connected=p.hid?hid_connected() && p.generation==s_hid_generation:cdc_connected() && p.generation==s_cdc_generation;
    r.status=!connected?ADV_DISCONNECTED:esp_timer_get_time()>=p.expires?STATUS_CARD_MISMATCH:
        Advanced_Validate(&previous,&p.request);
    if(!r.status && (card->protocol!=previous.protocol || card->uid_length!=previous.uid_len ||
        memcmp(card->uid,previous.uid,previous.uid_len)))r.status=STATUS_CARD_MISMATCH;
    if(!r.status){
        /* The next physical activation has now claimed the queued request.
         * Discovery/probing can take longer than the queue lease. USB expiry
         * must not cancel it halfway through, or allow a second write in.
         * Rebuild subtype/capacity from this activation, not host parameters. */
        static smartcard_info_t current_info;
        SmartCard_Probe(bus,card,&current_info);
        advanced_target_t current;Advanced_Describe(card,&current_info,&current);
        current.token=previous.token;
        connected=p.hid?hid_connected() && p.generation==s_hid_generation:cdc_connected() && p.generation==s_cdc_generation;
        if(!connected)r.status=ADV_DISCONNECTED;
        else Advanced_Execute(bus,card,&current,&p.request,&r);
    }
    advanced_reply(&r,p.hid);
    SKPass_CDC_Advanced_Clear();
    portENTER_CRITICAL(&s_write_lock);s_advanced_outstanding=false;portEXIT_CRITICAL(&s_write_lock);
}
static void ce_reply(uint32_t id, uint8_t status, uint16_t error, bool hid)
{
    uint8_t payload[CE_REPLY_BYTES]; cdc_frame_t frame;
    portENTER_CRITICAL(&s_write_lock);
    CE_Encode_Reply(id,status,s_ce_outstanding?CE_MODE_PENDING:s_ce_mode,s_ce_config.uid,error,payload);
    portEXIT_CRITICAL(&s_write_lock);
    frame.size=reader_encode_frame(CE_RESPONSE_MSG,payload,sizeof(payload),frame.bytes,sizeof(frame.bytes));
    if(xQueueSend(hid?s_hid_replies:s_replies,&frame,0)!=pdTRUE)++s_dropped;
}
bool SKPass_CDC_CE_Active(void)
{
    portENTER_CRITICAL(&s_write_lock);bool active=s_ce_mode!=CE_MODE_RW;
    portEXIT_CRITICAL(&s_write_lock);return active;
}
const card_emu_config_t *SKPass_CDC_CE_Config(void){return &s_ce_config;}
bool SKPass_CDC_CE_Pending(void){
    portENTER_CRITICAL(&s_write_lock);bool pending=s_ce_outstanding;
    portEXIT_CRITICAL(&s_write_lock);return pending;
}
bool SKPass_CDC_CE_Take(ce_request_t *request)
{
    if(!s_ce_requests || xQueueReceive(s_ce_requests,request,0)!=pdTRUE)return false;
    s_ce_hid=request->hid;
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    if(now-request->queued_ms>5000 || (request->hid?(!hid_connected() || request->generation!=s_hid_generation):(!cdc_connected() || request->generation!=s_cdc_generation))){
        portENTER_CRITICAL(&s_write_lock);s_ce_outstanding=false;portEXIT_CRITICAL(&s_write_lock);
        ce_reply(request->id,CE_DISCONNECTED,0,request->hid);return false;
    }
    portENTER_CRITICAL(&s_write_lock);
    if(request->action==CE_APPLY)s_ce_config=request->config;
    portEXIT_CRITICAL(&s_write_lock);
    xQueueReset(s_events);xQueueReset(s_hid_events);
    xQueueReset(s_details);xQueueReset(s_hid_details);
    return true;
}
void SKPass_CDC_CE_Result(uint32_t id,uint8_t status,uint8_t mode,const uint8_t uid[4],uint16_t error)
{
    portENTER_CRITICAL(&s_write_lock);
    s_ce_mode=mode;if(uid)memcpy(s_ce_config.uid,uid,4);
    if(id)s_ce_outstanding=false;
    portEXIT_CRITICAL(&s_write_lock);
    ce_reply(id,status,error,s_ce_hid);
}
void SKPass_CDC_CE_Event(uint8_t type,const uint8_t *data,size_t length)
{
    if(!s_ce_events || length>READER_MAX_PAYLOAD-CE_EVENT_HEADER || (!data && length))return;
    uint8_t payload[READER_MAX_PAYLOAD]={0};cdc_frame_t frame;
    payload[0]=1;payload[1]=type;
    unsigned sequence=++s_ce_sequence;
    for(unsigned i=0;i<4;i++)payload[2+i]=(uint8_t)(sequence>>(8*i));
    memcpy(payload+6,s_ce_config.uid,4);
    if(length)memcpy(payload+CE_EVENT_HEADER,data,length);
    frame.size=reader_encode_frame(CE_EVENT_MSG,payload,CE_EVENT_HEADER+length,frame.bytes,sizeof(frame.bytes));
    if(cdc_connected() && xQueueSend(s_ce_events,&frame,0)!=pdTRUE)++s_dropped;
    if(hid_connected() && xQueueSend(s_hid_ce_events,&frame,0)!=pdTRUE)++s_dropped;
}
static void host_frame(uint8_t msg,const uint8_t *p,size_t n,void *context)
{
    bool hid = context != NULL;
    if (hid && msg == SKPASS_HID_HELLO && n == 4) {
        uint32_t session=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
        if (!session) return;
        bool fresh = !hid_connected() || session != s_hid_session;
        if (fresh) close_hid_session();
        portENTER_CRITICAL(&s_write_lock);
        s_hid_active = true; s_hid_session = session;
        s_hid_keepalive = (uint32_t)(esp_timer_get_time()/1000);
        portEXIT_CRITICAL(&s_write_lock);
        {
            cdc_frame_t reply; uint8_t data[5]={1}; memcpy(data+1,p,4);
            reply.size = reader_encode_frame(SKPASS_HID_HELLO_REPLY,data,5,reply.bytes,sizeof(reply.bytes));
            xQueueSend(s_hid_replies,&reply,0);
        }
        if(fresh)refresh_advanced_target(true);
        return;
    }
    if (hid && msg == SKPASS_HID_CLOSE && n == 4) {
        uint32_t session=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
        if (session==s_hid_session) close_hid_session();
        return;
    }
    if (hid && !hid_connected()) return;
    QueueHandle_t replies = hid ? s_hid_replies : s_replies;
    if(msg==CE_REQUEST_MSG){
        ce_request_t request;
        if(!CE_Decode_Request(p,n,&request)){ce_reply(n>=4?(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24):0,CE_INVALID,0,hid);return;}
        if(request.action==CE_QUERY){ce_reply(request.id,CE_OK,0,hid);return;}
        portENTER_CRITICAL(&s_write_lock);
        bool busy=s_ce_outstanding || s_write_outstanding || s_advanced_outstanding || Rom_Download_Is_Requested() || Uf2_Update_Is_Requested();
        if(!busy)s_ce_outstanding=true;
        portEXIT_CRITICAL(&s_write_lock);
        if(busy){ce_reply(request.id,CE_BUSY,0,hid);return;}
        request.hid=hid;
        request.generation=hid?s_hid_generation:s_cdc_generation;request.queued_ms=(uint32_t)(esp_timer_get_time()/1000);
        /* Queue the acknowledgement before publishing work to the NFC task. */
        ce_reply(request.id,CE_ACCEPTED,0,hid);
        if(xQueueSend(s_ce_requests,&request,0)!=pdTRUE){
            portENTER_CRITICAL(&s_write_lock);s_ce_outstanding=false;portEXIT_CRITICAL(&s_write_lock);
            ce_reply(request.id,CE_BUSY,0,hid);return;
        }
        return;
    }
    if (msg == 0x31 && n == 0) {
        uint8_t payload[READER_MAX_PAYLOAD]; cdc_frame_t frame;
        size_t size = device_info(payload);
        if (!size) return;
        frame.size = reader_encode_frame(0xB1, payload, size,
                                         frame.bytes, sizeof(frame.bytes));
        if (xQueueSend(replies, &frame, 0) != pdTRUE)
            ESP_LOGW("SKPASS_CDC", "Device-info response queue full");
        return;
    }
    if ((msg == 0x30 && n == 0) || (msg == 0x34 && n == 1 && p[0] <= 1)) {
        portENTER_CRITICAL(&s_write_lock);
        bool busy = s_write_outstanding || s_advanced_outstanding || s_ce_outstanding;
        portEXIT_CRITICAL(&s_write_lock);
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (!busy && !Rom_Download_Is_Requested() && !Uf2_Update_Is_Requested())
            result = msg == 0x34 ? Rom_Download_Enter() : Uf2_Update_Enter();
        uint8_t status = busy ? 1 : result == ESP_OK ? 0 :
            result == ESP_ERR_NOT_SUPPORTED ? 3 : 2;
        cdc_frame_t frame;
        frame.size = reader_encode_frame(msg == 0x34 ? 0xB4 : 0xB0, &status, 1, frame.bytes, sizeof(frame.bytes));
        xQueueSend(replies, &frame, 0);
        return;
    }
    if(msg==ADV_REQUEST_MSG){
        advanced_request_t q;bool valid=Advanced_Decode_Request(p,n,&q);
        advanced_result_t r={.id=q.id,.target=q.target,.operation=q.operation,.address=q.address,.status=ADV_INVALID};
        portENTER_CRITICAL(&s_write_lock);
        bool busy=s_ce_mode!=CE_MODE_RW || s_ce_outstanding || s_write_outstanding || s_advanced_outstanding || Rom_Download_Is_Requested() || Uf2_Update_Is_Requested();
        advanced_target_t target=s_target;
        if(esp_timer_get_time()-s_target_seen>1800000)target.available=0;
        uint8_t status=!valid?ADV_INVALID:busy?ADV_BUSY:q.id==s_last_advanced_id?ADV_DUPLICATE:Advanced_Validate(&target,&q);
        if(!status){s_advanced_outstanding=true;s_last_advanced_id=q.id;}
        unsigned generation=hid?s_hid_generation:s_cdc_generation;
        portEXIT_CRITICAL(&s_write_lock);
        if(status){r.status=status;advanced_reply(&r,hid);return;}
        pending_advanced_t pending={.request=q,.expires=esp_timer_get_time()+2500000,.hid=hid,.generation=generation};
        if(xQueueSend(s_advanced,&pending,0)==pdTRUE)r.status=ADV_ACCEPTED;
        else {r.status=ADV_BUSY;portENTER_CRITICAL(&s_write_lock);s_advanced_outstanding=false;portEXIT_CRITICAL(&s_write_lock);}
        advanced_reply(&r,hid);return;
    }
    if (msg!=0x20) return; /* Only typed, bounded commands are exposed. */
    writer_request_t q={0};
    if (!writer_decode_request(p,n,&q)) {
        if (n>=4) q.id=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
        q.uid_len=0;memset(q.uid,0,sizeof(q.uid));
        writer_result_t r=request_reply(&q,WRITE_REJECTED);write_reply(&r,hid);return;
    }
    bool busy,duplicate;
    portENTER_CRITICAL(&s_write_lock);
    busy=s_ce_mode!=CE_MODE_RW || s_ce_outstanding || s_write_outstanding || s_advanced_outstanding || Rom_Download_Is_Requested() || Uf2_Update_Is_Requested();duplicate=s_last_write_id==q.id;
    if (!busy && !duplicate) {s_write_outstanding=true;s_write_disconnected=false;s_last_write_id=q.id;s_write_hid=hid;}
    portEXIT_CRITICAL(&s_write_lock);
    writer_result_t r=request_reply(&q,busy ? WRITE_BUSY : WRITE_REJECTED);
    if (busy || duplicate) {write_reply(&r,hid);return;}
    pending_write_t pending={.request=q,.expires=esp_timer_get_time()+15000000,.hid=hid};
    if (xQueueSend(s_writes,&pending,0)!=pdTRUE) {r.status=WRITE_BUSY;finish_write();}
    else r.status=WRITE_ACCEPTED;
    write_reply(&r,hid);
}
void SKPass_CDC_Check_Write_Timeout(void)
{
    pending_write_t p;
    if (!s_writes || xQueuePeek(s_writes,&p,0)!=pdTRUE) return;
    portENTER_CRITICAL(&s_write_lock);
    bool disconnected=s_write_disconnected;
    portEXIT_CRITICAL(&s_write_lock);
    uint8_t status=disconnected || !(p.hid ? hid_connected() : cdc_connected()) ? WRITE_DISCONNECTED :
                   esp_timer_get_time()>=p.expires ? WRITE_EXPIRED : 0;
    if (status && xQueueReceive(s_writes,&p,0)==pdTRUE) {
        writer_result_t r=request_reply(&p.request,status);SKPass_CDC_Write_Result(&r);
    }
}
bool SKPass_CDC_Take_Write(const pn7150_card_info_t *card, writer_request_t *q)
{
    SKPass_CDC_Check_Write_Timeout();
    pending_write_t p;
    if (!s_writes || xQueuePeek(s_writes,&p,0)!=pdTRUE ||
        !(p.hid ? hid_connected() : cdc_connected())) return false;
    if (card->uid_length!=p.request.uid_len || memcmp(card->uid,p.request.uid,p.request.uid_len)) return false;
    if (xQueueReceive(s_writes,&p,0)!=pdTRUE) return false;
    *q=p.request;return true;
}

bool SKPass_CDC_Should_Read(void)
{
    if (!s_events) return false;
    uint32_t now = (uint32_t)(esp_timer_get_time()/1000);
    if (s_has_event && now - s_last_event < CONFIG_SKPASS_SUPPRESS_MS) return false;
    s_has_event = true; s_last_event = now;
    return true;
}
void SKPass_USB_Debug(const char *text)
{
    if (!s_debug || !text) return;
    cdc_frame_t f;
    size_t n = strlen(text); if (n > READER_MAX_PAYLOAD) n = READER_MAX_PAYLOAD;
    f.size = reader_encode_frame(0x7F, (const uint8_t *)text, n, f.bytes, sizeof(f.bytes));
    if (xQueueSend(s_debug, &f, 0) != pdTRUE) ++s_dropped;
    if (hid_connected() && xQueueSend(s_hid_debug, &f, 0) != pdTRUE) ++s_dropped;
}
void SKPass_CDC_Card(const aime_card_event_t *event)
{
    if (!s_events) return;
    cdc_frame_t f; uint8_t payload[84];
    size_t n = Aime_Encode_Event(event, payload);
    f.size = reader_encode_frame(0x10, payload, n, f.bytes, sizeof(f.bytes));
    xQueueOverwrite(s_events, &f);
    if (hid_connected()) xQueueOverwrite(s_hid_events, &f);
}
void SKPass_CDC_SmartCard(const smartcard_info_t *info)
{
    if (!s_details || !info || info->records > SMARTCARD_MAX_RECORDS) return;
    /* A scan is an atomic logical document, carried in bounded frames. Older
     * queued documents may be superseded; scan/part lets hosts discard them. */
    xQueueReset(s_details); xQueueReset(s_hid_details);
    bool hid = hid_connected();
    for (unsigned part = 0; part <= info->records; ++part) {
        char payload[READER_MAX_PAYLOAD + 1]; cdc_frame_t frame;
        size_t n = SmartCard_Encode_Part(info, part, payload, sizeof(payload));
        if (!n || n > READER_MAX_PAYLOAD) {
            ESP_LOGW("SKPASS_CDC", "NFC detail encoding failed, part=%u", part);
            return;
        }
        frame.size = reader_encode_frame(SMARTCARD_DETAIL_MSG, (const uint8_t *)payload,
                                         n, frame.bytes, sizeof(frame.bytes));
        if (xQueueSend(s_details, &frame, 0) != pdTRUE) ++s_dropped;
        if (hid && xQueueSend(s_hid_details, &frame, 0) != pdTRUE) ++s_dropped;
    }
}
void SKPass_CDC_Removed(void)
{
    SKPass_CDC_Advanced_Clear();
    if (!s_debug) return;
    cdc_frame_t f;
    f.size = reader_encode_frame(0x11, NULL, 0, f.bytes, sizeof(f.bytes));
    if (xQueueSend(s_debug, &f, 0) != pdTRUE) ++s_dropped;
    if (hid_connected() && xQueueSend(s_hid_debug, &f, 0) != pdTRUE) ++s_dropped;
}
/* TinyUSB callbacks only enqueue. NFC commands stay in the existing NFC
 * task. HID and CDC dispatch share one I/O task, preserving write ordering. */
static void hid_step(void) {
    static skpass_hid_assembly_t assembly;
    static reader_decoder_t decoder;
    static cdc_frame_t current;
    static uint8_t sequence, index;
    static int64_t heartbeat;
    static bool connected;
    static unsigned generation;
    uint8_t report[64];
    for (unsigned i=0; i<16 && xQueueReceive(s_hid_rx,report,0)==pdTRUE; ++i) {
        int result = skpass_hid_receive(&assembly,report,64);
        if (result == 1) {
            decoder.size=0;
            reader_decoder_feed(&decoder,assembly.data,assembly.size,host_frame,(void *)1);
        }
    }
    if (!hid_connected()) {
        if (connected) close_hid_session();
        connected=false;current.size=0;index=0;heartbeat=0;
        return;
    }
    if (!connected || generation != s_hid_generation) {
        connected=true; generation=s_hid_generation; current.size=0; index=0; heartbeat=0;
    }
    if (!current.size) {
        index=0; ++sequence;
        if (xQueueReceive(s_hid_replies,&current,0)==pdTRUE) {}
        else if (!heartbeat || esp_timer_get_time()-heartbeat>=1000000) {
            current.size=reader_encode_frame(1,(const uint8_t *)s_firmware_id,
                strlen(s_firmware_id),current.bytes,sizeof(current.bytes));
            heartbeat=esp_timer_get_time();
        } else if (xQueueReceive(s_hid_targets,&current,0)==pdTRUE) {}
        else if (xQueueReceive(s_hid_ce_events,&current,0)==pdTRUE) {}
        else if (xQueueReceive(s_hid_debug,&current,0)==pdTRUE) {}
        else if (xQueueReceive(s_hid_events,&current,0)==pdTRUE) {}
        else xQueueReceive(s_hid_details,&current,0);
    }
    if (current.size && tud_hid_n_ready(1)) {
        skpass_hid_fragment(current.bytes,current.size,sequence,index,report);
        if (tud_hid_n_report(1,SKPASS_HID_REPORT_ID,report+1,63) &&
            ++index == skpass_hid_fragments(current.size)) current.size=0;
    }
}
static void cdc_task(void *arg)
{
    (void)arg;
    #if CONFIG_SKPASS_ENABLE_CDC
    cdc_frame_t current = {0}; size_t offset = 0;
    bool connected = false, event_frame = false;
    int64_t heartbeat = 0;
    unsigned prior_dropped = 0;
    reader_decoder_t decoder={0};
    #endif
    for (;;) {
        advanced_timeout();
        hid_step();
#if CONFIG_SKPASS_ENABLE_CDC
        uint8_t input[64];
        if (tud_cdc_available()) {
            uint32_t size=tud_cdc_read(input,sizeof(input));
            if (tud_cdc_connected()) reader_decoder_feed(&decoder,input,size,host_frame,NULL);
        }
        if (!cdc_connected()) {
            decoder.size=0;
            /* Sticky across quick unplug/reconnect: an old pending request
             * must not become eligible again just because DTR is now high. */
            portENTER_CRITICAL(&s_write_lock);
            if (s_write_outstanding && !s_write_hid) s_write_disconnected=true;
            portEXIT_CRITICAL(&s_write_lock);
            if (connected) {
                tud_cdc_write_clear(); offset = 0;
                xQueueReset(s_ce_events);
            }
            connected = false;
            if (event_frame) {
                cdc_frame_t newer;
                if (xQueueReceive(s_events, &newer, 0) == pdTRUE) current = newer;
            }
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        if (!connected) {
            connected = true;
            ESP_LOGI("SKPASS_CDC", "CDC connected; dropped diagnostics=%u", s_dropped);
            /* Raw startup text is deliberately outside binary framing; the
             * parser skips it. The heartbeat subsequently confirms protocol. */
            char ready[128];
            int ready_size = snprintf(ready, sizeof(ready), "%s ready\r\n", s_firmware_id);
            tud_cdc_write(ready, ready_size); tud_cdc_write_flush();
            heartbeat = esp_timer_get_time() - 1000000;
            refresh_advanced_target(false);
        }
        if (!current.size) {
            event_frame = false; offset = 0;
            if (xQueueReceive(s_replies,&current,0)==pdTRUE) {
                /* Write acknowledgements take priority over scan diagnostics. */
            } else if (esp_timer_get_time() - heartbeat >= 1000000) {
                current.size = reader_encode_frame(1, (const uint8_t *)s_firmware_id,
                             strlen(s_firmware_id), current.bytes, sizeof(current.bytes));
                heartbeat = esp_timer_get_time();
            } else if (xQueueReceive(s_targets, &current, 0) == pdTRUE) {
                /* Fresh typed target lease, before slow diagnostic streams. */
            } else if (xQueueReceive(s_ce_events, &current, 0) == pdTRUE) {
                /* Emulation events cannot delay local RF APDU replies. */
            } else if (xQueueReceive(s_debug, &current, 0) == pdTRUE) {
                /* Diagnostic records include IDm so old records cannot be
                 * mistaken for a newer activation. */
            } else if (xQueueReceive(s_events, &current, 0) == pdTRUE) event_frame = true;
            else if (xQueueReceive(s_details, &current, 0) == pdTRUE) {
                /* Public NFC metadata never changes legacy card events. */
            }
        }
        if (current.size) {
            offset += tud_cdc_write(current.bytes + offset, current.size - offset);
            tud_cdc_write_flush();
            if (offset == current.size) {current.size = 0; offset = 0; event_frame = false;}
        }
        if (s_dropped != prior_dropped) {
            ESP_LOGW("SKPASS_CDC", "Diagnostic queue full: dropped=%u", s_dropped);
            prior_dropped = s_dropped;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t SKPass_CDC_Init(void)
{
    identify_firmware();
#if CONFIG_SKPASS_ENABLE_CDC
    const tinyusb_config_cdcacm_t config = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_line_state_changed = cdc_line_changed,
    };
    esp_err_t ret = tusb_cdc_acm_init(&config);
    if (ret != ESP_OK) return ret;
#endif
    s_events = xQueueCreate(1, sizeof(cdc_frame_t));
    s_debug = xQueueCreate(96, sizeof(cdc_frame_t));
    s_replies = xQueueCreate(16,sizeof(cdc_frame_t));
    s_writes = xQueueCreate(1,sizeof(pending_write_t));
    s_hid_events = xQueueCreate(1,sizeof(cdc_frame_t));
    s_hid_debug = xQueueCreate(32,sizeof(cdc_frame_t));
    s_hid_replies = xQueueCreate(16,sizeof(cdc_frame_t));
    s_hid_rx = xQueueCreate(16,64);
    s_details = xQueueCreate(SMARTCARD_MAX_RECORDS + 1, sizeof(cdc_frame_t));
    s_hid_details = xQueueCreate(SMARTCARD_MAX_RECORDS + 1, sizeof(cdc_frame_t));
    s_advanced=xQueueCreate(1,sizeof(pending_advanced_t));
    s_targets=xQueueCreate(1,sizeof(cdc_frame_t));s_hid_targets=xQueueCreate(1,sizeof(cdc_frame_t));
    s_ce_requests=xQueueCreate(1,sizeof(ce_request_t));
    s_ce_events=xQueueCreate(24,sizeof(cdc_frame_t));s_hid_ce_events=xQueueCreate(24,sizeof(cdc_frame_t));
    if (!s_events || !s_debug || !s_replies || !s_writes ||
        !s_hid_events || !s_hid_debug || !s_hid_replies || !s_hid_rx ||
        !s_details || !s_hid_details || !s_advanced || !s_targets || !s_hid_targets ||
        !s_ce_requests || !s_ce_events || !s_hid_ce_events) return ESP_ERR_NO_MEM;
    if (xTaskCreate(cdc_task, "skpass_cdc", 6144, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI("SKPASS_CDC", "FW %s, CardIO + data HID + optional CDC", s_firmware_id);
    return ESP_OK;
}
