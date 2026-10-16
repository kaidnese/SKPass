/* Exercise the production driver against a deterministic PN7150 transport.
 * Including the source also lets us drive IRQ races without exposing test
 * hooks or changing the firmware's public API. */
#include <stdarg.h>
#include <stdlib.h>
#include "../main/pn7150.c"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

struct test_semaphore { bool pending; };
typedef struct { uint8_t bytes[258]; size_t size; } frame_t;
static struct test_semaphore semaphore;
static frame_t incoming[64], outgoing[64];
static size_t head, tail, sent_count;
static bool payload_read, irq_high, auto_reply, tvdd_matches;
static bool fail_add, fail_remove, fail_transmit, fail_receive, fail_semaphore;
static int get_mode, anti_tearing_mode;
static TickType_t ticks, last_wait_ticks;
static unsigned add_count, remove_count, take_count, delay_count, isr_add_count, yields;
static void (*isr)(void *);
static void *isr_arg;
static void (*on_wait)(void);
static char bus1, bus2, device1, device2;
static char logs[16384];
static size_t logs_used;
static const uint8_t init_rsp[] = {
    0x40, 0x01, 0x19, 0x00, 0x03, 0x1E, 0x03, 0x00, 0x08,
    0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x82, 0x83, 0x02,
    0xD0, 0x02, 0xFF, 0x02, 0x00, 0x04, 0x88, 0x10, 0x01, 0xA0
};

static void set_irq(bool high)
{
    bool rising = !irq_high && high;
    irq_high = high;
    if (rising && isr) isr(isr_arg);
}

static void enqueue(const uint8_t *bytes, size_t size)
{
    CHECK(tail < 64 && size <= 258 && size == 3u + bytes[2]);
    memcpy(incoming[tail].bytes, bytes, size);
    incoming[tail++].size = size;
    set_irq(true);
}

static void error_notification(void)
{
    const uint8_t frame[] = {0x60, 0x07, 0x01, 0xE6};
    enqueue(frame, sizeof(frame));
}

static void activated(uint8_t id, uint8_t protocol, uint8_t interface)
{
    uint8_t frame[] = {
        0x61, 0x05, 0x14, id, interface, protocol, 0x00, 0xFF, 0x01,
        0x09, 0x04, 0x00, 0x04, 0x0B, 0x58, 0x88, 0x4F, 0x01, 0x28,
        0x00, 0x00, 0x00, 0x00
    };
    if (protocol != 0x03) {
        enqueue(frame, sizeof(frame));
    } else {
        uint8_t felica[] = {
            0x61, 0x05, 0x1D, id, interface, protocol, 0x02, 0xFF, 0x01,
            0x12, 0x01, 0x10, 0x2E, 0x65, 0x77, 0x09, 0x93, 0xB0, 0x71, 0x00,
            0x00, 0xF1, 0x00, 0x00, 0x01, 0x43, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00
        };
        enqueue(felica, sizeof(felica));
    }
}

static void reset_mock(void)
{
    head = tail = sent_count = logs_used = 0;
    logs[0] = 0;
    payload_read = irq_high = auto_reply = false;
    tvdd_matches = true;
    fail_add = fail_remove = fail_transmit = fail_receive = fail_semaphore = false;
    get_mode = anti_tearing_mode = 0;
    ticks = last_wait_ticks = 0;
    add_count = remove_count = take_count = delay_count = isr_add_count = yields = 0;
    isr = NULL; isr_arg = NULL; on_wait = NULL;
    semaphore.pending = false;
    s_irq_semaphore = NULL; s_irq_handler_registered = false;
    s_device_handle = NULL; s_device_bus = NULL;
    s_tag_credits = s_tag_max_payload = 0;
    Pn7150_Init();
}

const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock error"; }
void test_log(const char *tag, const char *format, ...)
{
    (void)tag;
    va_list args;
    va_start(args, format);
    int count = vsnprintf(logs + logs_used, sizeof(logs) - logs_used, format, args);
    va_end(args);
    CHECK(count >= 0 && (size_t)count < sizeof(logs) - logs_used);
    logs_used += (size_t)count;
}
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t delay) { ++delay_count; ticks += delay; }
void test_yield_from_isr(void) { ++yields; }
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return fail_semaphore ? NULL : &semaphore;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait)
{
    CHECK(sem == &semaphore);
    if (wait) { ++take_count; last_wait_ticks = wait; }
    if (sem->pending) { sem->pending = false; return pdTRUE; }
    if (wait && on_wait) {
        void (*callback)(void) = on_wait;
        on_wait = NULL;
        callback();
    }
    if (sem->pending) { sem->pending = false; return pdTRUE; }
    ticks += wait;
    return pdFALSE;
}
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken)
{
    CHECK(sem == &semaphore);
    sem->pending = true;
    *woken = pdTRUE;
    return pdTRUE;
}
esp_err_t gpio_config(const gpio_config_t *config)
{
    if (config->pin_bit_mask == (1ULL << GPIO_IRQ_PIN)) {
        CHECK(config->mode == GPIO_MODE_INPUT && config->intr_type == GPIO_INTR_POSEDGE);
    }
    return ESP_OK;
}
int gpio_get_level(int pin) { CHECK(pin == GPIO_IRQ_PIN); return irq_high; }
esp_err_t gpio_set_level(int pin, int level)
{
    CHECK(pin == GPIO_VEN_PIN);
    if (!level) { head = tail = 0; payload_read = false; set_irq(false); }
    return ESP_OK;
}
esp_err_t gpio_install_isr_service(int flags)
{
    CHECK(flags == 0);
    return ESP_ERR_INVALID_STATE; /* shared ISR service already installed */
}
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg)
{
    CHECK(pin == GPIO_IRQ_PIN);
    ++isr_add_count; isr = handler; isr_arg = arg;
    return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *config, i2c_master_dev_handle_t *device)
{
    ++add_count;
    CHECK(config->scl_speed_hz == 400000 && config->device_address == 0x28);
    CHECK(config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    if (fail_add) { fail_add = false; return ESP_ERR_NO_MEM; }
    CHECK(bus == &bus1 || bus == &bus2);
    *device = bus == &bus1 ? &device1 : &device2;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    CHECK(device == &device1 || device == &device2);
    ++remove_count;
    if (fail_remove) { fail_remove = false; return ESP_FAIL; }
    return ESP_OK;
}
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout)
{
    CHECK(bus && address == 0x28 && timeout == 100);
    return ESP_OK;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t device,
    uint8_t *data, size_t size, int timeout)
{
    CHECK(device == &device1 || device == &device2);
    CHECK(timeout == 100 && head < tail && irq_high);
    if (fail_receive) { fail_receive = false; return ESP_FAIL; }
    frame_t *frame = &incoming[head];
    if (!payload_read) {
        CHECK(size == 3);
        memcpy(data, frame->bytes, 3);
        payload_read = frame->bytes[2] != 0;
        if (payload_read) return ESP_OK;
    } else {
        CHECK(size == frame->size - 3);
        memcpy(data, frame->bytes + 3, size);
        payload_read = false;
    }
    ++head;
    /* Consecutive packets may keep IRQ high: there need not be a new edge. */
    if (head == tail) set_irq(false);
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
    const uint8_t *data, size_t size, int timeout)
{
    CHECK(device == &device1 || device == &device2);
    CHECK(timeout == 100 && sent_count < 64 && size == 3u + data[2]);
    memcpy(outgoing[sent_count].bytes, data, size);
    outgoing[sent_count++].size = size;
    if (fail_transmit) { fail_transmit = false; return ESP_FAIL; }
    if (!auto_reply) return ESP_OK;
    if (data[0] == 0x20 && data[1] == 0x00) {
        CHECK(size == 4 && data[3] == 0x00); /* preserve configuration */
        const uint8_t response[] = {0x40, 0x00, 0x03, 0x00, 0x11, 0x00};
        if (anti_tearing_mode == 1) error_notification();
        enqueue(response, sizeof(response));
        if (anti_tearing_mode == 2) error_notification();
    } else if (data[0] == 0x20 && data[1] == 0x01) {
        enqueue(init_rsp, sizeof(init_rsp));
        if (anti_tearing_mode == 3) error_notification();
    } else if (data[0] == 0x20 && data[1] == 0x03) {
        const uint8_t expected[] = {0x20, 0x03, 0x03, 0x01, 0xA0, 0x0E};
        CHECK(size == sizeof(expected) && !memcmp(data, expected, size));
        if (get_mode == 1) return ESP_OK; /* transport timeout */
        if (get_mode == 2) {
            const uint8_t unsupported[] = {0x40, 0x03, 0x02, 0x09, 0x00};
            enqueue(unsupported, sizeof(unsupported));
        } else {
            uint8_t response[] = {0x40, 0x03, 0x08, 0x00, 0x01, 0xA0, 0x0E, 0x03, 0x06, 0x64, 0x00};
            if (!tvdd_matches) response[8] = 0x00;
            if (get_mode == 3) response[7] = 0x04; /* truncated value */
            if (get_mode == 4) response[6] = 0x0D; /* wrong parameter ID */
            enqueue(response, sizeof(response));
        }
    } else if (data[0] == 0x21 && data[1] == 0x04) {
        const uint8_t response[] = {0x41, 0x04, 0x01, 0x00};
        enqueue(response, sizeof(response));
        activated(data[3], data[4], data[5]);
    } else if (data[0] == 0x20 && data[1] == 0x02) {
        const uint8_t response[] = {0x40, 0x02, 0x02, 0x00, 0x00};
        if (data[4] == 0xA0) {
            const uint8_t expected[] = {0x20, 0x02, 0x07, 0x01, 0xA0, 0x0E, 0x03, 0x06, 0x64, 0x00};
            CHECK(size == sizeof(expected) && !memcmp(data, expected, size));
            tvdd_matches = true;
        } else if(data[3]==2 && data[4]==0x32){
            CHECK(size==13 && data[6]==0x20 && data[7]==0x33 && data[8]==4);
        } else {
            const uint8_t expected[] = {0x20, 0x02, 0x05, 0x01, 0x00, 0x02, 0x00, 0x01};
            CHECK(size == sizeof(expected) && !memcmp(data, expected, size));
        }
        enqueue(response, sizeof(response));
    } else {
        const uint8_t response[] = {(uint8_t)(data[0] + 0x20), data[1], 0x01, 0x00};
        if (data[0] == 0x21 && data[1] == 0x03) {
            if(data[3]==2){
                const uint8_t expected[]={0x21,3,5,2,0x80,1,0x82,1};
                CHECK(size==sizeof(expected) && !memcmp(data,expected,size));
            }else{
                const uint8_t expected[] = {0x21, 0x03, 0x09, 0x04, 0x00, 0x01, 0x01, 0x01, 0x02, 0x01, 0x06, 0x01};
                CHECK(size == sizeof(expected) && !memcmp(data, expected, size));
            }
        }
        enqueue(response, sizeof(response));
    }
    return ESP_OK;
}

static void irq_edge_during_wait(void) { ticks += 2; set_irq(true); }
static void irq_high_at_timeout(void) { irq_high = true; } /* no delivered edge */
static void stale_edge_during_wait(void) { ticks += 2; set_irq(true); set_irq(false); }

static void test_irq(void)
{
    reset_mock();
    Pn7150_Init();
    CHECK(isr_add_count == 1); /* init is idempotent */
    set_irq(true);
    CHECK(Pn7150_Wait_Irq(1000) == ESP_OK && ticks == 0 && take_count == 0);
    set_irq(false); /* old edge remains in binary semaphore */
    on_wait = irq_edge_during_wait;
    CHECK(Pn7150_Wait_Irq(1000) == ESP_OK && ticks == 2 && yields == 2);
    set_irq(false);
    Pn7150_Clear_Irq();
    ticks = 0; on_wait = stale_edge_during_wait;
    CHECK(Pn7150_Wait_Irq(1000) == ESP_ERR_TIMEOUT && ticks == 100);
    CHECK(last_wait_ticks == 98); /* stale edge does not restart deadline */
    on_wait = irq_high_at_timeout;
    CHECK(Pn7150_Wait_Irq(100) == ESP_OK); /* final level check */
    set_irq(false);
    CHECK(Pn7150_Wait_Irq(0) == ESP_ERR_TIMEOUT);
    CHECK(Pn7150_Wait_Irq(1) == ESP_ERR_TIMEOUT && last_wait_ticks == 1);
    semaphore.pending = true;
    Pn7150_Reset();
    CHECK(!semaphore.pending && !s_tag_credits && !s_tag_max_payload);
    CHECK(delay_count == 2); /* only hardware reset uses delays */

    s_irq_semaphore = NULL; s_irq_handler_registered = false; fail_semaphore = true;
    Pn7150_Init();
    CHECK(Pn7150_Wait_Irq(1000) == ESP_ERR_INVALID_STATE);
    fail_semaphore = false; Pn7150_Init();
    CHECK(s_irq_handler_registered);
    auto_reply = true;
    CHECK(Pn7150_Send_Core_Reset(&bus1) == ESP_OK);
    CHECK(Pn7150_Read_Core_Reset_Response(&bus1) == ESP_OK);
    CHECK(delay_count == 2); /* no polling delay on immediate response */
}

static void test_i2c(void)
{
    reset_mock(); auto_reply = true;
    CHECK(Pn7150_Test_ACK(&bus1));
    CHECK(Pn7150_Initialize(&bus1) == ESP_OK && add_count == 1 && remove_count == 0);
    Pn7150_Reset();
    CHECK(Pn7150_Initialize(&bus1) == ESP_OK && add_count == 1 && remove_count == 0);
    fail_remove = true;
    CHECK(Pn7150_Send_Core_Init(&bus2) == ESP_FAIL && add_count == 1);
    CHECK(Pn7150_Send_Core_Init(&bus2) == ESP_OK && add_count == 2 && remove_count == 2);
    CHECK(Pn7150_Read_Core_Init_Response(&bus2) == ESP_OK);
    fail_transmit = true;
    CHECK(Pn7150_Send_Core_Init(&bus2) == ESP_FAIL && add_count == 2);
    CHECK(Pn7150_Send_Core_Init(&bus2) == ESP_OK && add_count == 2);
    fail_receive = true;
    CHECK(Pn7150_Read_Core_Init_Response(&bus2) == ESP_FAIL);
    CHECK(Pn7150_Read_Core_Init_Response(&bus2) == ESP_OK);
    CHECK(Pn7150_Send_Core_Init(NULL) == ESP_ERR_INVALID_ARG);

    reset_mock(); auto_reply = true; fail_add = true;
    CHECK(Pn7150_Send_Core_Init(&bus1) == ESP_ERR_NO_MEM && !s_device_handle);
    CHECK(Pn7150_Send_Core_Init(&bus1) == ESP_OK && add_count == 2);
    CHECK(Pn7150_Read_Core_Init_Response(&bus1) == ESP_OK);
    /* Preserve credit accounting and fragmented tag DATA on the reused handle. */
    auto_reply = false; s_tag_credits = 1; s_tag_max_payload = 255;
    const uint8_t credit[] = {0x60, 0x06, 0x03, 0x01, 0x00, 0x01};
    const uint8_t part1[] = {0x10, 0x00, 0x02, 0x10, 0xAA};
    const uint8_t part2[] = {0x00, 0x00, 0x02, 0xBB, 0x00};
    enqueue(credit, sizeof(credit)); enqueue(part1, sizeof(part1)); enqueue(part2, sizeof(part2));
    uint8_t rx[8]; size_t size = 0;
    const uint8_t read[] = {0x10, 0x30, 0x01};
    CHECK(Pn7150_Tag_Transceive(&bus1, read, sizeof(read), rx, sizeof(rx), &size) == ESP_OK);
    const uint8_t expected[] = {0x10, 0xAA, 0xBB, 0x00};
    CHECK(size == sizeof(expected) && !memcmp(rx, expected, size));
    CHECK(s_tag_credits == 1 && add_count == 2 && remove_count == 0 && delay_count == 0);
}

static void discovery(uint8_t id, uint8_t protocol, uint8_t type)
{
    const uint8_t response[] = {0x61, 0x03, 0x05, id, protocol, 0x00, 0x00, type};
    enqueue(response, sizeof(response));
}
static void check_selection(const uint8_t *protocols, size_t count, size_t winner, uint8_t last_type)
{
    reset_mock(); auto_reply = true;
    for (size_t i = 0; i < count; ++i) discovery((uint8_t)(i + 1), protocols[i], i + 1 == count ? last_type : 2);
    pn7150_card_info_t card;
    CHECK(Pn7150_Read_Activated_Notification(&bus1, &card) == ESP_OK);
    CHECK(sent_count == 1 && head == tail && delay_count == 0);
    const uint8_t *select = outgoing[0].bytes;
    CHECK(select[0] == 0x21 && select[1] == 0x04 && select[3] == winner + 1);
    CHECK(select[4] == protocols[winner] && card.protocol == protocols[winner]);
    CHECK(card.discovery_id == winner + 1 && card.uid_length == (card.protocol == 3 ? 8u : 4u));
    CHECK(select[5] == (card.protocol == 0x80 ? 0x80 : card.protocol == 4 ? 2 : 1));
}
static void test_discovery(void)
{
    const uint8_t protocols[] = {0x04, 0x06, 0x02, 0x80};
    check_selection(protocols, 4, 3, 0);
    const uint8_t reverse[] = {0x80, 0x02, 0x06, 0x04};
    check_selection(reverse, 4, 0, 1);
    const uint8_t felica[] = {0x04, 0x03, 0x80};
    check_selection(felica, 3, 1, 0); /* equal priority keeps first */
    const uint8_t t2[] = {0x01, 0x04, 0x06, 0x02};
    check_selection(t2, 4, 3, 1);
    const uint8_t vicinity[] = {0x01, 0x04, 0x06};
    check_selection(vicinity, 3, 2, 0);
    const uint8_t iso_dep[] = {0x01, 0x04};
    check_selection(iso_dep, 2, 1, 1);
    reset_mock(); discovery(1, 0x80, 2);
    pn7150_card_info_t card;
    CHECK(Pn7150_Read_Activated_Notification(&bus1, &card) == ESP_ERR_INVALID_STATE && sent_count == 0);
    reset_mock(); discovery(1, 0x80, 3);
    CHECK(Pn7150_Read_Activated_Notification(&bus1, &card) == ESP_ERR_INVALID_RESPONSE && sent_count == 0);
    reset_mock();
    const uint8_t bad[] = {0x61, 0x03, 0x05, 1, 0x80, 0, 5, 0};
    enqueue(bad, sizeof(bad));
    CHECK(Pn7150_Read_Activated_Notification(&bus1, &card) == ESP_ERR_INVALID_RESPONSE);
    reset_mock(); activated(1, 0x80, 0x80);
    CHECK(Pn7150_Read_Activated_Notification(&bus1, &card) == ESP_OK && sent_count == 0);
    reset_mock();
    CHECK(Pn7150_Read_Activated_Notification(&bus1, &card) == ESP_ERR_TIMEOUT && delay_count == 0);
}

static size_t tvdd_writes(void)
{
    size_t count = 0;
    for (size_t i = 0; i < sent_count; ++i) {
        const uint8_t *cmd = outgoing[i].bytes;
        if (cmd[0] == 0x20 && cmd[1] == 0x02 && cmd[4] == 0xA0) ++count;
    }
    return count;
}
static void test_initialization(void)
{
    for (int anti = 0; anti <= 3; ++anti) {
        reset_mock(); auto_reply = true; anti_tearing_mode = anti;
        CHECK(Pn7150_Initialize(&bus1) == ESP_OK && tvdd_writes() == 0);
        CHECK(sent_count == 7 && head == tail);
        CHECK(strstr(logs, "ROM_CODE_V=0x10, FW_MAJOR_NO=0x01, FW_MINOR_NO=0xA0"));
        if (anti) CHECK(strstr(logs, "Anti-tearing"));
    }
    reset_mock(); auto_reply = true; tvdd_matches = false;
    CHECK(Pn7150_Initialize(&bus1) == ESP_OK && tvdd_writes() == 1);
    CHECK(Pn7150_Initialize(&bus1) == ESP_OK && tvdd_writes() == 1); /* recovery preserves EEPROM */
    reset_mock(); auto_reply = true; get_mode = 2;
    CHECK(Pn7150_Initialize(&bus1) == ESP_OK && tvdd_writes() == 1);
    for (int mode = 1; mode <= 4; ++mode) {
        if (mode == 2) continue;
        reset_mock(); auto_reply = true; get_mode = mode;
        CHECK(Pn7150_Initialize(&bus1) != ESP_OK && tvdd_writes() == 0);
    }
    reset_mock();
    const uint8_t empty[] = {0x40, 0x01, 0};
    enqueue(empty, sizeof(empty));
    CHECK(Pn7150_Read_Core_Init_Response(&bus1) == ESP_ERR_INVALID_RESPONSE);
    uint8_t malformed[sizeof(init_rsp)];
    memcpy(malformed, init_rsp, sizeof(malformed)); malformed[8] = 255;
    enqueue(malformed, sizeof(malformed));
    CHECK(Pn7150_Read_Core_Init_Response(&bus1) == ESP_ERR_INVALID_RESPONSE);
    malformed[8] = 8; malformed[2] -= 1;
    enqueue(malformed, sizeof(malformed) - 1);
    CHECK(Pn7150_Read_Core_Init_Response(&bus1) == ESP_ERR_INVALID_RESPONSE);
}

static void test_emulation(void)
{
    reset_mock();auto_reply=true;
    uint8_t uid[]={0x12,0x34,0x56,0x78};
    CHECK(Pn7150_Initialize_Card_Emulation(&bus1,uid)==ESP_OK);
    CHECK(outgoing[sent_count-3].bytes[2]==4 && outgoing[sent_count-2].bytes[2]==7 && outgoing[sent_count-1].bytes[2]==5);
    unsigned resets=0;for(size_t i=0;i<sent_count;i++){
        CHECK(outgoing[i].size==3u+outgoing[i].bytes[2]);
        if(outgoing[i].bytes[0]==0x20 && outgoing[i].bytes[1]==0)resets++;
    }
    CHECK(resets==2 && tvdd_writes()==0);
    auto_reply=false;
    const uint8_t activation[]={0x61,5,7,1,2,4,0x80,3,1,0};
    enqueue(activation,sizeof(activation));uint8_t data[261],kind;size_t n;
    CHECK(Pn7150_Card_Emulation_Receive(&bus1,data,sizeof(data),&n,&kind,100)==ESP_OK && kind==1 && s_tag_max_payload==3 && s_tag_credits==1);
    const uint8_t fragment[]={0x10,0,3,0,0xA4,4},last[]={0,0,2,0,7};
    const uint8_t credit[]={0x60,6,3,1,0,1};
    enqueue(fragment,sizeof(fragment));enqueue(credit,sizeof(credit));enqueue(last,sizeof(last));
    CHECK(Pn7150_Card_Emulation_Receive(&bus1,data,sizeof(data),&n,&kind,100)==ESP_OK && kind==2 && n==5 && data[2]==4 && s_tag_credits==2);
    uint8_t response[]={1,2,3,4,0x90,0};size_t before=sent_count;
    CHECK(Pn7150_Card_Emulation_Send(&bus1,response,sizeof(response))==ESP_OK);
    CHECK(sent_count==before+2 && outgoing[before].bytes[0]==0x10 && outgoing[before+1].bytes[0]==0 && s_tag_credits==0);
    enqueue(credit,sizeof(credit));CHECK(Pn7150_Card_Emulation_Send(&bus1,response,2)==ESP_OK);
    enqueue(fragment,sizeof(fragment));CHECK(Pn7150_Card_Emulation_Receive(&bus1,data,sizeof(data),&n,&kind,100)==ESP_ERR_INVALID_RESPONSE);
    const uint8_t deactivate[]={0x61,6,2,3,0};enqueue(deactivate,sizeof(deactivate));
    CHECK(Pn7150_Card_Emulation_Receive(&bus1,data,sizeof(data),&n,&kind,100)==ESP_OK && kind==3 && !s_tag_max_payload);
    const uint8_t idle[]={0x61,6,2,0,0};enqueue(idle,sizeof(idle));auto_reply=true;
    CHECK(Pn7150_Card_Emulation_Receive(&bus1,data,sizeof(data),&n,&kind,100)==ESP_OK && kind==3);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    if (!strcmp(argv[1], "irq")) test_irq();
    else if (!strcmp(argv[1], "i2c")) test_i2c();
    else if (!strcmp(argv[1], "discovery")) test_discovery();
    else if (!strcmp(argv[1], "initialization")) test_initialization();
    else if (!strcmp(argv[1], "emulation")) test_emulation();
    else CHECK(false);
    puts("PN7150 regression passed");
    return 0;
}
