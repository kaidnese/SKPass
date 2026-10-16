#include "uf2_update.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

#define UPDATE_MAGIC 0x43554632u
#define DISK_BLOCKS 32768u
#define FAT_SECTORS 128u
#define ROOT_START (1u + FAT_SECTORS)
#define DATA_START (ROOT_START + 2u)
#define PAYLOAD_SIZE 256u
#define APP_SIZE (6u * 1024u * 1024u)
#define MAX_BLOCKS (APP_SIZE / PAYLOAD_SIZE)
#define FAMILY_ID 0xC47E5767u
#define RESTART_COMMITTED (1u << 0)
#define RESTART_STATUS_SEEN (1u << 1)

static RTC_NOINIT_ATTR uint32_t s_boot_magic;
static bool s_active, s_requested, s_complete;
static TaskHandle_t s_restart_task;
static const esp_partition_t *s_target;
static esp_ota_handle_t s_ota;
static uint32_t s_blocks, s_received;
static uint8_t s_seen[MAX_BLOCKS / 8u];
static char s_failure[96];
static const char s_info[] =
    "UF2 Application Updater\r\nModel: PN7150 SKPass\r\n"
    "Board-ID: SKPASS-S3-N16R8-UF2-V1\r\nFamilyID: 0xc47e5767\r\n"
    "Addressing: application-relative, 256-byte payloads\r\n"
    "AutoRestart: after COMPLETE is read; 2-second grace, 17-second fallback\r\n"
    "Copy SKPass FIRMWARE.UF2. Read STATUS.TXT; restart is automatic.\r\n";

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
bool Uf2_Update_Is_Active(void) { return s_active; }
bool Uf2_Update_Is_Requested(void) { return s_active || s_requested; }
bool Uf2_Update_Has_Error(void) { return s_failure[0] != 0; }

static void enter_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500)); /* deliver CDC acknowledgement first */
    s_boot_magic = UPDATE_MAGIC;
    esp_restart();
}
esp_err_t Uf2_Update_Enter(void)
{
    if (s_requested) return ESP_ERR_INVALID_STATE;
    if (!esp_ota_get_next_update_partition(NULL)) return ESP_ERR_NOT_FOUND;
    s_requested = true;
    if (xTaskCreate(enter_task, "uf2_enter", 2048, NULL, 4, NULL) != pdPASS) {
        s_requested = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
bool Uf2_Update_Boot(void)
{
    bool requested = esp_reset_reason() == ESP_RST_SW && s_boot_magic == UPDATE_MAGIC;
    s_boot_magic = 0; /* one-shot; physical reset/power cycle restores SKPass */
    if (!requested) return false;
    s_active = true;
    s_target = esp_ota_get_next_update_partition(NULL);
    uint32_t flash_size = 0;
    if (!s_target || s_target == esp_ota_get_running_partition() || s_target->size != APP_SIZE)
        snprintf(s_failure, sizeof(s_failure), "Invalid OTA partition layout");
    else if (esp_flash_get_size(NULL, &flash_size) != ESP_OK || flash_size < 16u * 1024u * 1024u)
        snprintf(s_failure, sizeof(s_failure), "16 MB flash required");
    return true;
}
static void fail(const char *reason)
{
    if (!s_failure[0]) snprintf(s_failure, sizeof(s_failure), "%s", reason);
    if (s_ota) { esp_ota_abort(s_ota); s_ota = 0; }
    ESP_LOGE("UF2", "%s", s_failure);
}
static void restart_task(void *arg)
{
    (void)arg;
    uint32_t events = 0;
    /* The task exists before boot selection. Do not reboot until OTA
     * validation and selecting the new partition have both succeeded. */
    do {
        xTaskNotifyWait(0, 0, &events, portMAX_DELAY);
    } while (!(events & RESTART_COMMITTED));
    /* Give the desktop time to close/flush the file and read COMPLETE.
     * Explorer drag-and-drop need not read STATUS, so there is a fallback. */
    if (!(events & RESTART_STATUS_SEEN))
        xTaskNotifyWait(0, 0, &events, pdMS_TO_TICKS(15000));
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI("UF2", "Verified firmware complete; restarting into new image");
    s_boot_magic = 0; /* Never re-enter MSC on the completion reboot. */
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
static void receive_block(const uint8_t *b)
{
    /* Ignore FAT, directory and OS metadata writes, as UF2 specifies. */
    if (get32(b) != 0x0A324655u || get32(b + 4) != 0x9E5D5157u) return;
    /* Boot selection is the commit point. Ignore subsequent OS writes or
     * retries; they cannot turn committed success into an ERROR claiming
     * that the old firmware is still selected. */
    if (s_failure[0] || s_complete) return;
    uint32_t address = get32(b + 12), number = get32(b + 20), count = get32(b + 24);
    if (get32(b + 508) != 0x0AB16F30u || get32(b + 8) != 0x2000u ||
        get32(b + 28) != FAMILY_ID || get32(b + 16) != PAYLOAD_SIZE ||
        count == 0 || count > MAX_BLOCKS || number >= count ||
        address != number * PAYLOAD_SIZE) {
        fail("Invalid SKPass UF2 block, family or address"); return;
    }
    if (s_blocks && count != s_blocks) { fail("Mixed UF2 images"); return; }
    if (!s_blocks) {
        s_blocks = count;
        if (esp_ota_begin(s_target, count * PAYLOAD_SIZE, &s_ota) != ESP_OK) {
            s_ota = 0; fail("Cannot prepare inactive OTA partition"); return;
        }
    }
    uint8_t mask = (uint8_t)(1u << (number & 7));
    if (s_seen[number / 8] & mask) {
        uint8_t prior[PAYLOAD_SIZE];
        if (esp_partition_read(s_target, address, prior, sizeof(prior)) != ESP_OK ||
            memcmp(prior, b + 32, sizeof(prior))) fail("Conflicting duplicate UF2 block");
        return;
    }
    if (esp_ota_write_with_offset(s_ota, b + 32, PAYLOAD_SIZE, address) != ESP_OK) {
        fail("OTA flash write failed"); return;
    }
    s_seen[number / 8] |= mask;
    ++s_received;
    if (s_received != s_blocks) return;
    esp_err_t ret = esp_ota_end(s_ota);
    s_ota = 0;
    if (ret != ESP_OK) { fail("Firmware image checksum/hash validation failed"); return; }
    esp_app_desc_t description;
    if (esp_ota_get_partition_description(s_target, &description) != ESP_OK ||
        (strncmp(description.project_name, "SKPass", sizeof(description.project_name)) &&
         strncmp(description.project_name, "pn7150_cardio", sizeof(description.project_name)))) {
        fail("Not a SKPass application image"); return;
    }
    if (xTaskCreate(restart_task, "uf2_restart", 2048, NULL, 4,
                    &s_restart_task) != pdPASS) {
        s_restart_task = NULL;
        fail("Cannot prepare automatic restart; old firmware remains selected"); return;
    }
    if (esp_ota_set_boot_partition(s_target) != ESP_OK) {
        vTaskDelete(s_restart_task); s_restart_task = NULL;
        fail("Cannot select verified firmware"); return;
    }
    s_complete = true;
    xTaskNotify(s_restart_task, RESTART_COMMITTED, eSetBits);
    ESP_LOGI("UF2", "Verified firmware ready; automatic restart pending");
}
static void entry(uint8_t *e, const char *name, uint8_t attributes, uint16_t cluster, uint32_t size)
{
    memcpy(e, name, 11); e[11] = attributes; put16(e + 26, cluster); put32(e + 28, size);
}
static void sector(uint32_t lba, uint8_t *b)
{
    memset(b, 0, 512);
    if (lba == 0) {
        b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90; memcpy(b + 3, "MSDOS5.0", 8);
        put16(b + 11, 512); b[13] = 1; put16(b + 14, 1); b[16] = 1;
        put16(b + 17, 32); put16(b + 19, DISK_BLOCKS); b[21] = 0xF8;
        put16(b + 22, FAT_SECTORS); put16(b + 24, 32); put16(b + 26, 64);
        b[36] = 0x80; b[38] = 0x29; put32(b + 39, 0x43554632u);
        memcpy(b + 43, "SKPASS_UF2 ", 11); memcpy(b + 54, "FAT16   ", 8);
        b[510] = 0x55; b[511] = 0xAA;
    } else if (lba == 1) {
        put16(b, 0xFFF8); put16(b + 2, 0xFFFF);
        put16(b + 4, 0xFFFF); put16(b + 6, 0xFFFF);
    } else if (lba == ROOT_START) {
        entry(b, "SKPASS_UF2 ", 0x08, 0, 0);
        entry(b + 32, "INFO_UF2TXT", 0x21, 2, sizeof(s_info) - 1);
        entry(b + 64, "STATUS  TXT", 0x21, 3, 512);
    } else if (lba == DATA_START) {
        memcpy(b, s_info, sizeof(s_info) - 1);
    } else if (lba == DATA_START + 1) {
        if (s_failure[0]) snprintf((char *)b, 512, "ERROR: %s\r\nOld firmware remains selected. Reconnect to retry.\r\n", s_failure);
        else if (s_complete) snprintf((char *)b, 512, "COMPLETE\r\nImage verified. Device will restart automatically.\r\n");
        else snprintf((char *)b, 512, "READY\r\nReceived: %lu/%lu blocks\r\n", (unsigned long)s_received, (unsigned long)s_blocks);
    }
}
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4])
{
    (void)lun; memcpy(vendor, "SKPass  ", 8); memcpy(product, "UF2 Updater     ", 16); memcpy(revision, "2.2 ", 4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) { (void)lun; return s_active; }
void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size)
{ (void)lun; *count = DISK_BLOCKS; *size = 512; }
bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return s_active; }
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    (void)lun;
    if (lba >= DISK_BLOCKS || offset > 512 || size > 512 - offset) return -1;
    uint8_t b[512]; sector(lba, b); memcpy(buffer, b + offset, size);
    if (lba == DATA_START + 1 && offset == 0 && size >= 8 && s_complete && s_restart_task)
        xTaskNotify(s_restart_task, RESTART_STATUS_SEEN, eSetBits);
    return size;
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    (void)lun;
    if (!s_active || lba >= DISK_BLOCKS || offset != 0 || size != 512) return -1;
    if (lba >= DATA_START + 2) receive_block(buffer);
    return s_failure[0] ? -1 : (int32_t)size;
}
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool eject)
{ (void)lun; (void)power; (void)start; (void)eject; return true; }
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t size)
{
    (void)buffer; (void)size;
    if (command[0] == 0x35) return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0); return -1;
}
