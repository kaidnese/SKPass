#include "skpass_usb.h"

#include <string.h>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tusb.h"

#include "SKPass.h"
#include "skpass_cdc.h"
#include "uf2_update.h"
#include "hid_wire.h"

#ifndef CONFIG_SKPASS_PLAYER
#define CONFIG_SKPASS_PLAYER 1
#endif

#ifndef CONFIG_SKPASS_SUPPRESS_MS
#define CONFIG_SKPASS_SUPPRESS_MS 3000
#endif

#define TAG "SKPASS_USB"

#define SKPASS_USB_VID             0x1CCFu
#define SKPASS_USB_PID             0x5252u
#define SKPASS_USB_BCD_DEVICE      0x0200u
#define SKPASS_USB_EP_IN           0x81u
#define SKPASS_USB_EP_SIZE         16u
#define SKPASS_USB_POLL_INTERVAL   1u
#define SKPASS_SEND_RETRY          (-4)

#if CONFIG_SKPASS_ENABLE_CDC
#define SKPASS_TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#define SKPASS_INTERFACE_COUNT 4 /* CardIO 0, CDC 1/2, SKPass data HID 3 */
#define SKPASS_DATA_INTERFACE 3
#else
#define SKPASS_TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#define SKPASS_INTERFACE_COUNT 2
#define SKPASS_DATA_INTERFACE 1
#endif

#if CFG_TUD_HID < 2
#error "SKPass requires CONFIG_TINYUSB_HID_COUNT=2"
#endif
static const uint8_t s_data_report_descriptor[] = {
    0x06, 0xCB, 0xFF, /* vendor Usage Page FFCB, distinct from CardIO FFCA */
    0x09, 0x01, 0xA1, 0x01,
    0x85, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x3F,
    0x09, 0x01, 0x81, 0x02, /* Input: ID 1 + 63 bytes */
    0x09, 0x02, 0x91, 0x02, /* Output: ID 1 + 63 bytes */
    0xC0
};

#if CONFIG_SKPASS_PLAYER == 2
#define SKPASS_USB_SERIAL "CARDIOP2"
#else
#define SKPASS_USB_SERIAL "CARDIOP1"
#endif

static SKPass s_skpass;
static QueueHandle_t s_pending_queue;

/* Preserve the SKPass identity and HID interface zero. Composite mode uses
 * the IAD device class; the rollback path retains the original class zero. */
static const tusb_desc_device_t s_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
#if CONFIG_SKPASS_ENABLE_CDC
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
#endif
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = SKPASS_USB_VID,
    .idProduct          = SKPASS_USB_PID,
    .bcdDevice          = SKPASS_USB_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations  = 0x01,
};

static const char *s_string_descriptor[] = {
    (const char[]){0x09, 0x04}, /* English (United States) */
    "SKPass",
    "SKPass",
    SKPASS_USB_SERIAL,
    "SKPass HID",
    "SKPass debug CDC",
    "SKPass UF2 updater",
    "SKPass data HID",
};

static const uint8_t s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, SKPASS_INTERFACE_COUNT, 0, SKPASS_TUSB_DESC_TOTAL_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_NONE,
                       sizeof(SKPass_HID_ReportDescriptor),
                       SKPASS_USB_EP_IN, SKPASS_USB_EP_SIZE,
                       SKPASS_USB_POLL_INTERVAL),
#if CONFIG_SKPASS_ENABLE_CDC
    TUD_CDC_DESCRIPTOR(1, 5, 0x82, 8, 0x03, 0x83, 64),
#endif
    TUD_HID_INOUT_DESCRIPTOR(SKPASS_DATA_INTERFACE, 7, HID_ITF_PROTOCOL_NONE,
                            sizeof(s_data_report_descriptor), 0x04, 0x84, 64, 1),
};

/* Distinct identity lets Windows enumerate the MSC-only update personality. */
static tusb_desc_device_t s_update_device;
static const uint8_t s_update_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(0, 6, 0x01, 0x81, 64),
};

static int skpass_hid_send_report(const uint8_t report[SKPASS_REPORT_BYTES],
                                  void *context)
{
    (void)context;

    if (report == NULL || (report[0] != SKPASS_TYPE_ISO15693 &&
                           report[0] != SKPASS_TYPE_FELICA_OR_ISO14443)) {
        return -1;
    }

    if (!tud_mounted() || !tud_hid_ready()) {
        return -1;
    }

    /* tud_hid_report() adds the report ID.  Pass only bytes 1..8 here so
     * the host receives exactly the nine-byte SKPass report once. */
    return tud_hid_report(report[0], &report[1], SKPASS_CARD_BYTES) ?
           (int)SKPASS_REPORT_BYTES : -1;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance == 0 ? SKPass_HID_ReportDescriptor : s_data_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    /* SKPass only sends input reports; unsupported GET_REPORT requests stall. */
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    if (instance != 1 || report_type != HID_REPORT_TYPE_OUTPUT) return;
    /* Interrupt OUT includes Report ID; control SET_REPORT has it in wValue. */
    if (report_id == 0 && bufsize == 64 && buffer[0] == SKPASS_HID_REPORT_ID)
        SKPass_HID_Receive(buffer);
    else if (report_id == SKPASS_HID_REPORT_ID && bufsize == 63) {
        uint8_t report[64]; report[0] = report_id;
        memcpy(report + 1, buffer, 63); SKPass_HID_Receive(report);
    }
}

static uint32_t skpass_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static int skpass_send_card_now(const pn7150_card_info_t *card_info)
{
    /* Discard a pre-enumeration card queued before a CE mode transition. */
    if(SKPass_CDC_CE_Active() || SKPass_CDC_CE_Pending())return -1;
    if (card_info == NULL || card_info->uid_length == 0 ||
        card_info->uid_length > PN7150_NFCID_MAX_LENGTH) {
        return -1;
    }

    uint8_t payload[SKPASS_CARD_BYTES] = {0};
    uint8_t report_type;

    switch (card_info->mode_technology) {
    case 0x02: /* NFC-F / FeliCa: IDm is already eight bytes. */
        if (card_info->uid_length != SKPASS_CARD_BYTES) {
            return -2;
        }
        memcpy(payload, card_info->uid, SKPASS_CARD_BYTES);
        report_type = SKPASS_TYPE_FELICA_OR_ISO14443;
        break;

    case 0x06: /* ISO15693: pn7150.c has already canonicalized the UID. */
        if (card_info->uid_length != SKPASS_CARD_BYTES) {
            return -2;
        }
        memcpy(payload, card_info->uid, SKPASS_CARD_BYTES);
        report_type = SKPASS_TYPE_ISO15693;
        break;

    case 0x00: /* NFC-A / ISO14443 */
    case 0x01: /* NFC-B / ISO14443-B */
        memcpy(payload, card_info->uid,
               card_info->uid_length < SKPASS_CARD_BYTES ?
               card_info->uid_length : SKPASS_CARD_BYTES);
        /* Preserve the compatibility rule from SKPass_FromISO14443(). */
        payload[0] &= 0x0Fu;
        report_type = SKPASS_TYPE_FELICA_OR_ISO14443;
        break;

    default:
        ESP_LOGW(TAG, "不支持通过 SKPass 上报的 RF 技术: 0x%02X",
                 card_info->mode_technology);
        return -3;
    }

    if (!tud_mounted() || !tud_hid_ready()) {
        return SKPASS_SEND_RETRY;
    }

    int result = SKPass_Send(&s_skpass, report_type, payload, skpass_now_ms());
    if (result == 0) {
        ESP_LOGD(TAG, "SKPass 抑制重复报告 (%u ms)",
                 (unsigned)CONFIG_SKPASS_SUPPRESS_MS);
    } else if (result > 0) {
        ESP_LOGI(TAG, "SKPass HID 报告已发送 (type=%u)",
                 (unsigned)report_type);
    }
    return result;
}

static void skpass_usb_retry_task(void *arg)
{
    (void)arg;
    pn7150_card_info_t card_info;

    while (1) {
        if (xQueueReceive(s_pending_queue, &card_info, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        while (1) {
            int result = skpass_send_card_now(&card_info);
            if (result >= 0 || result != SKPASS_SEND_RETRY) {
                break;
            }

            /* Keep the most recent card event in the queue while waiting for
             * enumeration.  A later scan replaces this pending value. */
            vTaskDelay(pdMS_TO_TICKS(100));
            pn7150_card_info_t newer_card;
            if (xQueueReceive(s_pending_queue, &newer_card, 0) == pdTRUE) {
                card_info = newer_card;
            }
        }
    }
}

static esp_err_t install_usb_with_reenumeration(const tinyusb_config_t *config)
{
    esp_err_t ret = tinyusb_driver_install(config);
    if (ret != ESP_OK) return ret;

    /* ROM CDC and SKPass use different device identities on the same PHY.
     * DWC2 initialization disconnects and connects immediately; keep the
     * initialized stack detached long enough for the host/hub to retire the
     * previous ROM (or MSC) device before requesting our descriptors. This
     * also applies after watchdog reset, which does not remove USB power. */
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    tud_connect();
    ESP_LOGI(TAG, "USB 已断开 500ms 并重新连接，等待主机枚举");
    return ESP_OK;
}

esp_err_t SKPass_USB_Init(void)
{
    if (Uf2_Update_Is_Active()) {
        s_update_device = s_device_descriptor;
        s_update_device.idProduct = 0x5253;
        s_update_device.bDeviceClass = 0;
        s_update_device.bDeviceSubClass = 0;
        s_update_device.bDeviceProtocol = 0;
        const tinyusb_config_t update = {
            .device_descriptor = &s_update_device,
            .string_descriptor = s_string_descriptor,
            .string_descriptor_count = sizeof(s_string_descriptor) / sizeof(s_string_descriptor[0]),
            .configuration_descriptor = s_update_configuration,
        };
        return install_usb_with_reenumeration(&update);
    }
    const tinyusb_config_t config = {
        .device_descriptor = &s_device_descriptor,
        .string_descriptor = s_string_descriptor,
        .string_descriptor_count = sizeof(s_string_descriptor) /
                                   sizeof(s_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = s_configuration_descriptor,
    };

    SKPass_Init(&s_skpass, skpass_hid_send_report, NULL);
    SKPass_SetSuppressTime(&s_skpass, CONFIG_SKPASS_SUPPRESS_MS);

    s_pending_queue = xQueueCreate(1, sizeof(pn7150_card_info_t));
    if (s_pending_queue == NULL) {
        ESP_LOGE(TAG, "创建 SKPass 待发送队列失败");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = install_usb_with_reenumeration(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB 安装失败: %s", esp_err_to_name(ret));
        return ret;
    }

    if (xTaskCreate(skpass_usb_retry_task, "skpass_usb", 3072, NULL, 4,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建 SKPass USB 任务失败");
        return ESP_ERR_NO_MEM;
    }
    ret = SKPass_CDC_Init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "SKPass HID 已初始化 (VID=0x%04X, PID=0x%04X, 序列号=%s)",
             (unsigned)SKPASS_USB_VID, (unsigned)SKPASS_USB_PID,
             SKPASS_USB_SERIAL);
    return ESP_OK;
}

esp_err_t SKPass_USB_SendCard(const pn7150_card_info_t *card_info)
{
    if (card_info == NULL || card_info->uid_length == 0 ||
        card_info->uid_length > PN7150_NFCID_MAX_LENGTH ||
        s_pending_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* A length-one queue deliberately keeps only the newest card.  This
     * avoids blocking the PN7150 task while a PC is still enumerating USB. */
    xQueueOverwrite(s_pending_queue, card_info);
    return ESP_OK;
}
