#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_rom_sys.h"
#include "driver/i2c_master.h"
#include "pn7150.h"
#include "card_emulation.h"

#define TAG "PN7150"
#define PN7150_PRESENCE_CHECK_INTERVAL_MS 500
static uint8_t s_tag_credits;
static uint8_t s_tag_max_payload;
static bool (*s_interrupt_check)(void);
void Pn7150_Set_Interrupt_Check(bool (*check)(void)){s_interrupt_check=check;}
static bool interrupted(void){return s_interrupt_check && s_interrupt_check();}
static SemaphoreHandle_t s_irq_semaphore;
static bool s_irq_handler_registered;
/* All NCI I/O is serialized by the NFC task. The bus must stay alive while
 * its device is attached; a change of bus detaches the previous device once. */
static i2c_master_bus_handle_t s_device_bus;
static i2c_master_dev_handle_t s_device_handle;

/* NXP PN7150 RF front-end tuning profile.  The signature is stored in the
 * extended EEPROM parameter A0 14 so recovery boots do not rewrite A0 0D. */
static const uint8_t RF_CONFIG_MAGIC[4] = {0x53, 0x4B, 0x50, 0x01};
static const uint8_t get_rf_version_cmd[] = {
    0x20, 0x03, 0x03, 0x01, 0xA0, 0x14
};
static const uint8_t set_rf_version_cmd[] = {
    0x20, 0x02, 0x08, 0x01,
    0xA0, 0x14, 0x04, 0x53, 0x4B, 0x50, 0x01
};
static const uint8_t s_rf_conf_cmd[] = {
    /* NCI length includes the parameter count byte plus all 17 TLVs:
     * 1 + (9*12 + 7*5) = 148 = 0x94. */
    0x20, 0x02, 0x94, 0x11,
    0xA0, 0x0D, 0x06, 0x04, 0x35, 0x90, 0x01, 0xF4, 0x01,
    0xA0, 0x0D, 0x06, 0x06, 0x30, 0x01, 0x90, 0x03, 0x00,
    0xA0, 0x0D, 0x06, 0x06, 0x42, 0x02, 0x00, 0xFF, 0xFF,
    0xA0, 0x0D, 0x06, 0x20, 0x42, 0x88, 0x00, 0xFF, 0xFF,
    0xA0, 0x0D, 0x04, 0x22, 0x44, 0x23, 0x00,
    0xA0, 0x0D, 0x06, 0x22, 0x2D, 0x50, 0x34, 0x0C, 0x00,
    0xA0, 0x0D, 0x06, 0x32, 0x42, 0xF8, 0x00, 0xFF, 0xFF,
    0xA0, 0x0D, 0x06, 0x34, 0x2D, 0x24, 0x37, 0x0C, 0x00,
    0xA0, 0x0D, 0x06, 0x34, 0x33, 0x86, 0x80, 0x00, 0x70,
    0xA0, 0x0D, 0x04, 0x34, 0x44, 0x22, 0x00,
    0xA0, 0x0D, 0x06, 0x42, 0x2D, 0x15, 0x45, 0x0D, 0x00,
    0xA0, 0x0D, 0x04, 0x46, 0x44, 0x22, 0x00,
    0xA0, 0x0D, 0x06, 0x46, 0x2D, 0x05, 0x59, 0x0E, 0x00,
    0xA0, 0x0D, 0x06, 0x44, 0x42, 0x88, 0x00, 0xFF, 0xFF,
    0xA0, 0x0D, 0x06, 0x56, 0x2D, 0x05, 0x9F, 0x0C, 0x00,
    0xA0, 0x0D, 0x06, 0x54, 0x42, 0x88, 0x00, 0xFF, 0xFF,
    0xA0, 0x0D, 0x06, 0x0A, 0x33, 0x80, 0x86, 0x00, 0x70
};
_Static_assert(sizeof(s_rf_conf_cmd) == 151,
               "PN7150 A0 0D profile length must match its 17 TLVs");
static esp_err_t Pn7150_Receive_Nci_Packet(i2c_master_bus_handle_t bus_handle,
    uint8_t *response, size_t response_capacity, size_t *response_size,
    uint32_t timeout_ms);

static void IRAM_ATTR Pn7150_Irq_Handler(void *arg)
{
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)arg, &task_woken);
    if (task_woken == pdTRUE) portYIELD_FROM_ISR();
}

static void Pn7150_Clear_Irq(void)
{
    if (s_irq_semaphore) xSemaphoreTake(s_irq_semaphore, 0);
}

static esp_err_t Pn7150_Wait_Irq(uint32_t timeout_ms)
{
    /* IRQ stays high until the queued NCI packet is read. Check its level
     * even if its edge happened before the ISR was installed. */
    if (gpio_get_level(GPIO_IRQ_PIN)) return ESP_OK;
    if (!s_irq_semaphore || !s_irq_handler_registered) return ESP_ERR_INVALID_STATE;
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms && !timeout_ticks) timeout_ticks = 1;
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout_ticks) {
            return gpio_get_level(GPIO_IRQ_PIN) ? ESP_OK : ESP_ERR_TIMEOUT;
        }
        BaseType_t signaled = xSemaphoreTake(s_irq_semaphore, timeout_ticks - elapsed);
        if (gpio_get_level(GPIO_IRQ_PIN)) return ESP_OK;
        if (signaled != pdTRUE) return ESP_ERR_TIMEOUT;
        /* A stale edge must not authorize an I2C read while IRQ is low.
         * Continue waiting within the original deadline, without polling. */
    }
}

static esp_err_t Pn7150_Get_Device(i2c_master_bus_handle_t bus_handle,
                                  i2c_master_dev_handle_t *device_handle)
{
    if (!bus_handle || !device_handle) return ESP_ERR_INVALID_ARG;
    if (s_device_handle && s_device_bus != bus_handle) {
        esp_err_t ret = i2c_master_bus_rm_device(s_device_handle);
        if (ret != ESP_OK) return ret;
        s_device_handle = NULL;
        s_device_bus = NULL;
    }
    if (!s_device_handle) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PN7150_I2C_ADDRESS,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };
        i2c_master_dev_handle_t device = NULL;
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &config, &device);
        if (ret != ESP_OK) return ret;
        s_device_handle = device;
        s_device_bus = bus_handle;
    }
    *device_handle = s_device_handle;
    return ESP_OK;
}

static esp_err_t Pn7150_Send_Nci_Packet(i2c_master_bus_handle_t bus_handle,
                                      const uint8_t *packet, size_t size)
{
    i2c_master_dev_handle_t device;
    esp_err_t ret = Pn7150_Get_Device(bus_handle, &device);
    if (ret != ESP_OK) return ret;
    Pn7150_Clear_Irq();
    /* Clear before transmitting, never after: a fast response may already
     * have raised IRQ by the time transmit returns. Wait_Irq checks level. */
    return i2c_master_transmit(device, packet, size, 100);
}

static esp_err_t Pn7150_Read_Command_Response(i2c_master_bus_handle_t bus_handle,
    uint8_t expected_first, uint8_t expected_second, uint8_t *response,
    size_t response_capacity, size_t *response_size);

static esp_err_t tag_credits(const uint8_t *packet, size_t size)
{
    if (size < 4 || size != 4u + 2u * packet[3]) return ESP_ERR_INVALID_RESPONSE;
    for (size_t i = 0; i < packet[3]; ++i) {
        size_t p = 4 + i * 2;
        if ((packet[p] & 0x0F) == 0 && s_tag_credits != 0xFF) {
            unsigned total = s_tag_credits + packet[p + 1];
            s_tag_credits = total > 254 ? 254 : (uint8_t)total;
        }
    }
    return ESP_OK;
}

static const uint8_t s_rf_discover_cmd[] = {
    0x21, 0x03, 0x09, 0x04,
    0x00, 0x01,  /* NFC-A passive poll */
    0x01, 0x01,  /* NFC-B passive poll */
    0x02, 0x01,  /* NFC-F passive poll */
    0x06, 0x01   /* ISO15693 passive poll */
};

/* Reader/writer protocol-to-interface mapping used by the NXP-derived
 * ElectronicCats driver. ISO-DEP must use the ISO-DEP interface so that the
 * proprietary presence-check command is valid after activation. */
static const uint8_t s_rf_discover_map_cmd[] = {
    0x21, 0x00, 0x10, 0x05,
    0x01, 0x01, 0x01,  /* T1T -> Frame */
    0x02, 0x01, 0x01,  /* T2T -> Frame */
    0x03, 0x01, 0x01,  /* T3T -> Frame */
    0x04, 0x01, 0x02,  /* ISO-DEP -> ISO-DEP */
    0x80, 0x01, 0x80   /* MIFARE -> Tag command */
};

void Pn7150_Init(void)
{
    if (!s_irq_semaphore) s_irq_semaphore = xSemaphoreCreateBinary();
    if (!s_irq_semaphore) {
        ESP_LOGE(TAG, "创建 PN7150 IRQ 信号量失败");
        return;
    }
    // 配置 VEN 引脚为输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_VEN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置 VEN 失败: %s", esp_err_to_name(ret));
        return;
    }

    // 配置 IRQ 引脚为输入模式
    io_conf.pin_bit_mask = (1ULL << GPIO_IRQ_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置 IRQ 失败: %s", esp_err_to_name(ret));
        return;
    }

    if (!s_irq_handler_registered) {
        ret = gpio_install_isr_service(0);
        /* Other GPIO users may already own the shared ISR service. */
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            ret = gpio_isr_handler_add(GPIO_IRQ_PIN, Pn7150_Irq_Handler,
                                       s_irq_semaphore);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "注册 PN7150 IRQ 中断失败: %s", esp_err_to_name(ret));
            return;
        }
        s_irq_handler_registered = true;
    }

    // 保持 VEN 为高电平，不执行复位
    gpio_set_level(GPIO_VEN_PIN, 1);
}

void Pn7150_WakeUp(void)
{
    gpio_set_level(GPIO_VEN_PIN, 1); // VEN 保持高电平
}

void Pn7150_Reset(void)
{
    /* Recover the NFCC independently of the ESP32 and USB tasks. */
    gpio_set_level(GPIO_VEN_PIN, 0);
    Pn7150_Clear_Irq();
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(GPIO_VEN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    Pn7150_Clear_Irq();
    s_tag_credits = 0;
    s_tag_max_payload = 0;
}

/**
 * @brief 测试 PN7150 是否在 I2C 总线上响应
 *
 * @param bus_handle I2C 主机总线句柄
 * @return true - PN7150 响应 ACK
 *         false - PN7150 无响应
 */
bool Pn7150_Test_ACK(i2c_master_bus_handle_t bus_handle)
{
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C 总线句柄无效");
        return false;
    }

    ESP_LOGI(TAG, "正在测试 PN7150...");
    ESP_LOGI(TAG, "  7位地址: 0x%02X", PN7150_I2C_ADDRESS);
    ESP_LOGI(TAG, "  写地址: 0x%02X", PN7150_I2C_WRITE_ADDR);
    ESP_LOGI(TAG, "  读地址: 0x%02X", PN7150_I2C_READ_ADDR);

    // ESP-IDF 的 i2c_master_probe 使用 7 位地址
    esp_err_t ret = i2c_master_probe(bus_handle, PN7150_I2C_ADDRESS, 100);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ PN7150 响应 ACK - 设备在线");
        return true;
    }

    int irq_level = gpio_get_level(GPIO_IRQ_PIN);
    if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "✗ PN7150 无响应 - 超时，GPIO7 电平: %d", irq_level);
    } else {
        ESP_LOGE(TAG, "✗ PN7150 测试失败: %s，GPIO7 电平: %d",
                 esp_err_to_name(ret), irq_level);
    }
    return false;
}

esp_err_t Pn7150_Send_Core_Reset(i2c_master_bus_handle_t bus_handle)
{
    /* ResetType=0 keeps configuration; resetting to defaults here would
     * defeat the A0 0E read-before-write protection on every recovery. */
    static const uint8_t core_reset_cmd[] = {0x20, 0x00, 0x01, 0x00};

    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "发送 CORE_RESET_CMD 失败：I2C 总线句柄无效");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = Pn7150_Send_Nci_Packet(bus_handle, core_reset_cmd,
                                         sizeof(core_reset_cmd));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "已发送 CORE_RESET_CMD: 20 00 01 00 (保留配置)");
    } else {
        ESP_LOGE(TAG, "发送 CORE_RESET_CMD 失败: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t Pn7150_Read_Core_Reset_Response(i2c_master_bus_handle_t bus_handle)
{
    static const uint8_t expected_header[] = {0x40, 0x00, 0x03, 0x00};
    uint8_t response[3 + 255] = {0};
    size_t response_size = 0;

    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "读取 CORE_RESET_RSP 失败：I2C 总线句柄无效");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = Pn7150_Read_Command_Response(bus_handle, 0x40, 0x00,
        response, sizeof(response), &response_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取 CORE_RESET_RSP 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    printf("PN7150 响应: ");
    for (size_t index = 0; index < response_size; index++) {
        printf("%02X%s", response[index],
               index + 1 == response_size ? "\n" : " ");
    }

    if (response_size != 6) return ESP_ERR_INVALID_RESPONSE;

    for (size_t index = 0; index < sizeof(expected_header); index++) {
        if (response[index] != expected_header[index]) {
            ESP_LOGE(TAG, "CORE_RESET_RSP 头校验失败，期望 40 00 03 00");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    ESP_LOGI(TAG, "CORE_RESET_RSP 头校验通过");
    return ESP_OK;
}

esp_err_t Pn7150_Send_Core_Init(i2c_master_bus_handle_t bus_handle)
{
    static const uint8_t core_init_cmd[] = {0x20, 0x01, 0x00};

    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "发送 CORE_INIT_CMD 失败：I2C 总线句柄无效");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = Pn7150_Send_Nci_Packet(bus_handle, core_init_cmd,
                                         sizeof(core_init_cmd));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "已发送 CORE_INIT_CMD: 20 01 00");
    } else {
        ESP_LOGE(TAG, "发送 CORE_INIT_CMD 失败: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t Pn7150_Read_Core_Init_Response(i2c_master_bus_handle_t bus_handle)
{
    uint8_t response[3 + 255] = {0};
    size_t response_size = 0;

    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "读取 CORE_INIT_RSP 失败：I2C 总线句柄无效");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = Pn7150_Read_Command_Response(bus_handle, 0x40, 0x01,
        response, sizeof(response), &response_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取 CORE_INIT_RSP 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    printf("PN7150 CORE_INIT_RSP: ");
    for (size_t index = 0; index < response_size; index++) {
        printf("%02X%s", response[index],
               index + 1 == response_size ? "\n" : " ");
    }

    if (response_size < 9 || response[0] != 0x40 ||
        response[1] != 0x01 || response[3] != 0x00) {
        ESP_LOGE(TAG, "CORE_INIT_RSP 校验失败，期望 40 01 开头且状态码为 00");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* PN7150 NCI 1.x: interface count is response[8]. The variable interface
     * list precedes NXP's manufacturer information and ROM/FW fields. */
    size_t version_offset = 17u + response[8];
    if (version_offset + 3 > response_size) {
        ESP_LOGE(TAG, "CORE_INIT_RSP 固件版本字段不完整");
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "PN7150 ROM_CODE_V=0x%02X, FW_MAJOR_NO=0x%02X, FW_MINOR_NO=0x%02X",
             response[version_offset], response[version_offset + 1],
             response[version_offset + 2]);

    ESP_LOGI(TAG, "CORE_INIT_RSP 头校验通过");
    return ESP_OK;
}

static esp_err_t Pn7150_Read_Command_Response(i2c_master_bus_handle_t bus_handle,
    uint8_t expected_first, uint8_t expected_second, uint8_t *response,
    size_t response_capacity, size_t *response_size)
{
    if (!bus_handle || !response || !response_size || response_capacity < 3) {
        return ESP_ERR_INVALID_ARG;
    }
    *response_size = 0;
    TickType_t start = xTaskGetTickCount();
    do {
        esp_err_t ret = Pn7150_Receive_Nci_Packet(bus_handle, response,
                                      response_capacity, response_size, 100);
        if (ret == ESP_ERR_TIMEOUT) continue;
        if (ret != ESP_OK) return ret;
        if (response[0] == expected_first && response[1] == expected_second) return ESP_OK;
        if (response[0] == 0x60 && response[1] == 0x06) {
            ret = tag_credits(response, *response_size);
            if (ret != ESP_OK) return ret;
        }
        if (response[0] == 0x60 && response[1] == 0x07) {
            if (*response_size != 4) return ESP_ERR_INVALID_RESPONSE;
            if (response[3] == 0xE6) {
                ESP_LOGW(TAG, "PN7150 Anti-tearing 通知：RF 配置已恢复，将读回核对 TVDD");
            } else {
                ESP_LOGW(TAG, "CORE_GENERIC_ERROR_NTF: status=0x%02X", response[3]);
            }
            continue;
        }
        /* A credit notification or late tag DATA is not a command response.
         * Bound the wait, but do not mistake either for RF_DEACTIVATE_RSP. */
        ESP_LOGD(TAG, "等待命令响应时跳过 NCI: %02X %02X", response[0], response[1]);
    } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(1000));
    return ESP_ERR_TIMEOUT;
}

static esp_err_t Pn7150_Transceive(i2c_master_bus_handle_t bus_handle,
    const uint8_t *command, size_t command_size, uint8_t *response,
    size_t response_capacity, size_t *response_size)
{
    if (!bus_handle || !command || command_size < 3 || !response ||
        !response_size || response_capacity < 3) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = Pn7150_Send_Nci_Packet(bus_handle, command, command_size);
    if (ret != ESP_OK) return ret;
    return Pn7150_Read_Command_Response(bus_handle, (uint8_t)(command[0] + 0x20),
        command[1], response, response_capacity, response_size);
}

static esp_err_t Pn7150_Check_Command_Response(i2c_master_bus_handle_t bus_handle,
                                                const uint8_t *command,
                                                size_t command_size,
                                                uint8_t expected_first,
                                                uint8_t expected_second,
                                                const char *command_name,
                                                bool log_success)
{
    uint8_t response[3 + 255] = {0};
    size_t response_size = 0;
    esp_err_t ret = Pn7150_Transceive(bus_handle, command, command_size,
                                      response, sizeof(response), &response_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s 收发失败: %s", command_name, esp_err_to_name(ret));
        return ret;
    }

    bool response_valid = response_size >= 4 &&
                          response[0] == expected_first &&
                          response[1] == expected_second &&
                          response[3] == 0x00;
    if (log_success || !response_valid) {
        ESP_LOGI(TAG, "%s RX (%u 字节):", command_name,
                 (unsigned)response_size);
        for (size_t index = 0; index < response_size; index++) {
            printf("%02X%s", response[index],
                   index + 1 == response_size ? "\n" : " ");
        }
    }

    if (!response_valid) {
        ESP_LOGE(TAG, "%s 响应校验失败", command_name);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (log_success) {
        ESP_LOGI(TAG, "%s 响应校验通过", command_name);
    }
    return ESP_OK;
}

static esp_err_t Pn7150_Configure_Tvdd(i2c_master_bus_handle_t bus_handle)
{
    /* Read the extended NXP parameter before writing nonvolatile settings.
     * Do not alter the A0 0D RF/antenna configuration. */
    static const uint8_t get_config_cmd[] = {0x20, 0x03, 0x03, 0x01, 0xA0, 0x0E};
    static const uint8_t set_config_cmd[] = {
        0x20, 0x02, 0x07, 0x01,
        0xA0, 0x0E, 0x03, 0x06, 0x64, 0x00
    };
    uint8_t response[3 + 255];
    size_t size = 0;
    esp_err_t ret = Pn7150_Transceive(bus_handle, get_config_cmd,
        sizeof(get_config_cmd), response, sizeof(response), &size);
    if (ret != ESP_OK) return ret;
    if (size < 5) return ESP_ERR_INVALID_RESPONSE;
    if (response[3] == 0x00) {
        /* Successful GET_CONFIG: status, parameter count, extended ID,
         * length and value. Reject truncated/extra TLVs instead of using
         * unverified bytes to make the EEPROM decision. */
        if (response[4] != 1 || size < 8 || response[5] != 0xA0 ||
            response[6] != 0x0E || size != 8u + response[7]) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (response[7] == 3 && !memcmp(response + 8, set_config_cmd + 7, 3)) {
            ESP_LOGI(TAG, "A0 0E 已为 06 64 00，跳过重复写入");
            return ESP_OK;
        }
        ESP_LOGI(TAG, "A0 0E 与外部 5V 配置不同，更新 TVDD");
    } else {
        ESP_LOGW(TAG, "CORE_GET_CONFIG A0 0E status=0x%02X，尝试设置外部 5V 配置",
                 response[3]);
    }
    return Pn7150_Check_Command_Response(bus_handle, set_config_cmd,
        sizeof(set_config_cmd), 0x40, 0x02, "CORE_SET_CONFIG_TVDD_CMD", true);
}

static esp_err_t Pn7150_Configure_Rf_Profile(i2c_master_bus_handle_t bus_handle,
                                               bool *updated)
{
    uint8_t response[3 + 255] = {0};
    size_t size = 0;
    bool current = false;
    esp_err_t ret = Pn7150_Transceive(bus_handle, get_rf_version_cmd,
                                      sizeof(get_rf_version_cmd), response,
                                      sizeof(response), &size);
    if (ret == ESP_OK && size >= 12 && response[3] == 0x00 &&
        response[4] == 1 && response[5] == 0xA0 && response[6] == 0x14 &&
        response[7] == sizeof(RF_CONFIG_MAGIC) &&
        !memcmp(response + 8, RF_CONFIG_MAGIC, sizeof(RF_CONFIG_MAGIC))) {
        current = true;
    } else if (ret != ESP_OK) {
        /* If the marker cannot be read, do not silently bypass the requested
         * RF profile.  The controller may report a transient GET_CONFIG
         * error before private parameters have been provisioned; proceed with
         * the TVDD/A0 0D write and let the command response decide whether
         * this firmware accepts the profile. */
        ESP_LOGW(TAG, "读取 A0 14 射频版本失败：%s，继续载入 A0 0E/A0 0D 射频表",
                 esp_err_to_name(ret));
    } else if (size < 4 || response[3] != 0x00) {
        /* A readable marker is preferred for EEPROM wear protection, but a
         * missing/unsupported marker must not make the RF profile disappear.
         * Provision TVDD and A0 0D now; a later successful GET_CONFIG can
         * use the SKP01 marker to suppress subsequent rewrites. */
        ESP_LOGW(TAG, "PN7150 不支持可读的 A0 14 版本参数，仍继续载入 A0 0E/A0 0D 射频表");
    }

    if (current) {
        ESP_LOGD(TAG, "EEPROM 射频参数已是最新 (SKP01)，跳过重复烧写");
        if (updated) *updated = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "检测到未配置或过期的射频参数，正在向 EEPROM 烧录 A0 0E 与 A0 0D 射频表...");
    ret = Pn7150_Configure_Tvdd(bus_handle);
    if (ret != ESP_OK) return ret;
    ret = Pn7150_Check_Command_Response(bus_handle, s_rf_conf_cmd,
                                        sizeof(s_rf_conf_cmd), 0x40, 0x02,
                                        "CORE_SET_CONFIG_RF_A0_0D_CMD", true);
    if (ret != ESP_OK) return ret;
    ret = Pn7150_Check_Command_Response(bus_handle, set_rf_version_cmd,
                                        sizeof(set_rf_version_cmd), 0x40, 0x02,
                                        "CORE_SET_CONFIG_RF_VERSION_CMD", true);
    if (ret == ESP_OK && updated) *updated = true;
    return ret;
}

esp_err_t Pn7150_Initialize(i2c_master_bus_handle_t bus_handle)
{
    s_tag_credits = 0;
    s_tag_max_payload = 0;
    static const uint8_t proprietary_activate_cmd[] = {0x2F, 0x02, 0x00};
    /* TOTAL_DURATION is a standard 16-bit little-endian value in ms:
     * 00 01 = 256 ms, the PN7150 reference reader/writer configuration. */
    static const uint8_t total_duration_cmd[] = {
        0x20, 0x02, 0x05, 0x01, 0x00, 0x02, 0x00, 0x01
    };
    if (Pn7150_Send_Core_Reset(bus_handle) != ESP_OK ||
        Pn7150_Read_Core_Reset_Response(bus_handle) != ESP_OK ||
        Pn7150_Send_Core_Init(bus_handle) != ESP_OK ||
        Pn7150_Read_Core_Init_Response(bus_handle) != ESP_OK) {
        return ESP_FAIL;
    }

    bool rf_updated = false;
    esp_err_t rf_profile_ret = Pn7150_Configure_Rf_Profile(bus_handle, &rf_updated);
    if (rf_profile_ret != ESP_OK) {
        /* RF tuning is an optional EEPROM enhancement. Some PN7150 ROM/FW
         * revisions reject private A0 14/A0 0D parameters; keep the stock
         * controller configuration usable instead of trapping NFC in the
         * recovery loop. */
        ESP_LOGW(TAG, "A0 0D 射频调优未被控制器接受，保留现有配置继续启动: %s",
                 esp_err_to_name(rf_profile_ret));
        rf_updated = false;
    }

    /* A0 0D is latched by CORE_INIT. Reload the controller only after a
     * changed profile, then continue with the normal activation sequence. */
    if (rf_updated) {
        if (Pn7150_Send_Core_Reset(bus_handle) != ESP_OK ||
            Pn7150_Read_Core_Reset_Response(bus_handle) != ESP_OK ||
            Pn7150_Send_Core_Init(bus_handle) != ESP_OK ||
            Pn7150_Read_Core_Init_Response(bus_handle) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if (Pn7150_Check_Command_Response(bus_handle, proprietary_activate_cmd,
                                      sizeof(proprietary_activate_cmd),
                                      0x4F, 0x02,
                                      "NCI_PROPRIETARY_ACT_CMD", true) != ESP_OK ||
        Pn7150_Check_Command_Response(bus_handle, total_duration_cmd,
                                      sizeof(total_duration_cmd), 0x40, 0x02,
                                      "CORE_SET_CONFIG_TOTAL_DURATION_CMD", true) != ESP_OK ||
        Pn7150_Check_Command_Response(bus_handle, s_rf_discover_map_cmd,
                                      sizeof(s_rf_discover_map_cmd), 0x41, 0x00,
                                      "RF_DISCOVER_MAP_CMD", true) != ESP_OK ||
        Pn7150_Check_Command_Response(bus_handle, s_rf_discover_cmd,
                                      sizeof(s_rf_discover_cmd), 0x41, 0x03,
                                      "RF_DISCOVER_CMD", true) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t Pn7150_Receive_Nci_Packet(i2c_master_bus_handle_t bus_handle,
                                            uint8_t *response,
                                            size_t response_capacity,
                                            size_t *response_size,
                                            uint32_t timeout_ms)
{
    if (bus_handle == NULL || response == NULL || response_size == NULL ||
        response_capacity < 3) {
        return ESP_ERR_INVALID_ARG;
    }

    *response_size = 0;
    esp_err_t ret = Pn7150_Wait_Irq(timeout_ms);
    if (ret != ESP_OK) return ret;

    i2c_master_dev_handle_t device_handle = NULL;
    ret = Pn7150_Get_Device(bus_handle, &device_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_master_receive(device_handle, response, 3, 100);
    if (ret == ESP_OK) {
        size_t payload_size = response[2];
        if (payload_size > response_capacity - 3) {
            ret = ESP_ERR_INVALID_SIZE;
        } else if (payload_size > 0) {
            ret = i2c_master_receive(device_handle, response + 3,
                                     payload_size, 100);
        }
        if (ret == ESP_OK) {
            *response_size = 3 + payload_size;
        }
    }
    return ret;
}

static unsigned Pn7150_Protocol_Priority(uint8_t protocol)
{
    switch (protocol) {
    case 0x80: /* MIFARE Classic */
    case 0x03: /* T3T FeliCa */ return 5;
    case 0x02: /* T2T */ return 4;
    case 0x06: /* ISO15693 */ return 3;
    case 0x04: /* ISO-DEP */ return 2;
    default: return 1;
    }
}

esp_err_t Pn7150_Read_Activated_Notification(i2c_master_bus_handle_t bus_handle,
                                             pn7150_card_info_t *card_info)
{
    uint8_t response[3 + 255] = {0};
    size_t response_size = 0;
    bool have_discovered_target = false;
    bool awaiting_selected_activation = false;
    TickType_t activation_wait_start = xTaskGetTickCount();
    uint8_t target_discovery_id = 0;
    uint8_t target_protocol = 0;
    uint8_t target_interface = 0;

    if (card_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *card_info = (pn7150_card_info_t){0};

    while (1) {
        if (xTaskGetTickCount() - activation_wait_start >= pdMS_TO_TICKS(2000)) {
            return (awaiting_selected_activation || have_discovered_target) ?
                   ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
        }
        if(interrupted())return ESP_ERR_TIMEOUT;
        esp_err_t ret = Pn7150_Receive_Nci_Packet(bus_handle, response,
                                                   sizeof(response),
                                                   &response_size, 100);
        if (ret != ESP_OK) {
            if(ret==ESP_ERR_TIMEOUT && !interrupted())continue;
            if (ret != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "读取 NCI 通知失败: %s", esp_err_to_name(ret));
            }
            return ret == ESP_ERR_TIMEOUT &&
                   (awaiting_selected_activation || have_discovered_target) ?
                   ESP_ERR_INVALID_STATE : ret;
        }

        if (response_size >= 10 && response[0] == 0x61 &&
            response[1] == 0x05) {
            break;
        }

        if (response_size >= 8 && response[0] == 0x61 &&
            response[1] == 0x03) {
            size_t params_size = response[6];
            size_t notification_type_index = 7 + params_size;
            if (notification_type_index + 1 != response_size ||
                response[notification_type_index] > 0x02) {
                ESP_LOGE(TAG, "RF_DISCOVER_NTF 参数长度无效");
                return ESP_ERR_INVALID_RESPONSE;
            }

            /* Equal priority keeps the first candidate for a stable choice.
             * Compare every notification, including the final candidate. */
            if (!have_discovered_target ||
                Pn7150_Protocol_Priority(response[4]) >
                Pn7150_Protocol_Priority(target_protocol)) {
                target_discovery_id = response[3];
                target_protocol = response[4];
                target_interface = target_protocol == 0x04 ? 0x02 :
                                   target_protocol == 0x05 ? 0x03 :
                                   target_protocol == 0x80 ? 0x80 : 0x01;
                have_discovered_target = true;
            }

            uint8_t notification_type = response[notification_type_index];
            if (notification_type == 0x00 || notification_type == 0x01) {
                uint8_t rf_discover_select_cmd[] = {
                    0x21, 0x04, 0x03,
                    target_discovery_id, target_protocol, target_interface
                };
                ret = Pn7150_Check_Command_Response(bus_handle,
                                                     rf_discover_select_cmd,
                                                     sizeof(rf_discover_select_cmd),
                                                     0x41, 0x04,
                                                     "RF_DISCOVER_SELECT_CMD",
                                                     false);
                if (ret != ESP_OK) {
                    return ret;
                }
                have_discovered_target = false;
                awaiting_selected_activation = true;
            }
            continue;
        }

        if (response_size >= 3 && response[0] == 0x61 &&
            response[1] == 0x06) {
            ESP_LOGD(TAG, "忽略已排队的 RF_DEACTIVATE_NTF");
            continue;
        }

        if (response_size >= 4 && response[0] == 0x60 &&
            response[1] == 0x07) {
            if (response[3] == 0xA1) {
                ESP_LOGD(TAG, "目标激活失败，继续轮询");
            } else {
                ESP_LOGW(TAG, "CORE_GENERIC_ERROR_NTF: status=0x%02X",
                         response[3]);
            }
            continue;
        }

        ESP_LOGW(TAG, "等待卡片时忽略 NCI 报文: %02X %02X，长度 %u",
                 response[0], response[1], (unsigned)response_size);
        ESP_LOG_BUFFER_HEXDUMP(TAG, response, response_size, ESP_LOG_DEBUG);
    }

    ESP_LOG_BUFFER_HEXDUMP(TAG, response, response_size, ESP_LOG_DEBUG);

    card_info->discovery_id = response[3];
    card_info->rf_interface = response[4];
    card_info->protocol = response[5];
    card_info->mode_technology = response[6];
    s_tag_max_payload = response[7];
    s_tag_credits = response[8];

    size_t params_size = response[9];
    size_t params_start = 10;
    if (params_start + params_size > response_size) {
        ESP_LOGE(TAG, "RF 技术参数长度无效");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (card_info->mode_technology == 0x00) {
        if (params_size < 3) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        size_t uid_size = response[params_start + 2];
        size_t uid_start = params_start + 3;
        if (uid_size == 0 || uid_size > PN7150_NFCID_MAX_LENGTH ||
            uid_start + uid_size > params_start + params_size) {
            ESP_LOGE(TAG, "NFC-A UID 长度无效: %u", (unsigned)uid_size);
            return ESP_ERR_INVALID_RESPONSE;
        }

        card_info->uid_length = uid_size;
        for (size_t index = 0; index < uid_size; index++) {
            card_info->uid[index] = response[uid_start + index];
        }
        size_t sel_index = uid_start + uid_size;
        if (sel_index + 1 < params_start + params_size && response[sel_index] == 1) {
            card_info->sak = response[sel_index + 1];
            card_info->sak_valid = true;
        }
    } else if (card_info->mode_technology == 0x01) {
        if (params_size < 5) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        size_t sensb_size = response[params_start];
        if (sensb_size < 4 || sensb_size + 1 > params_size) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        size_t uid_start = params_start + 1;
        if (sensb_size >= 5 && response[uid_start] == 0x50) {
            uid_start++;
        }
        card_info->uid_length = 4;
        for (size_t index = 0; index < card_info->uid_length; index++) {
            card_info->uid[index] = response[uid_start + index];
        }
    } else if (card_info->mode_technology == 0x02) {
        if (params_size < 10) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        size_t sensf_size = response[params_start + 1];
        if (sensf_size < 8 || sensf_size + 2 > params_size) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        size_t uid_start = params_start + 2;
        /* NCI SENSF_RES normally excludes the 0x01 response code. An IDm
         * starting with 01 must not be shifted. Only accept a code-bearing
         * variant when its length unambiguously identifies that layout. */
        if ((sensf_size == 17 || sensf_size == 19) && response[uid_start] == 0x01) {
            uid_start++;
        }
        card_info->uid_length = 8;
        for (size_t index = 0; index < card_info->uid_length; index++) {
            card_info->uid[index] = response[uid_start + index];
        }
        if (uid_start + 16 <= params_start + params_size) {
            memcpy(card_info->pmm, response + uid_start + 8, 8);
            card_info->pmm_valid = true;
        }
        if (uid_start + 18 <= params_start + params_size) {
            memcpy(card_info->system_code, response + uid_start + 16, 2);
            card_info->system_code_valid = true;
        }
    } else if (card_info->mode_technology == 0x06) {
        if (params_size < 10) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        card_info->uid_length = 8;
        for (size_t index = 0; index < card_info->uid_length; index++) {
            card_info->uid[7 - index] = response[params_start + 2 + index];
        }
    } else {
        ESP_LOGW(TAG, "暂不支持从 RF 技术模式 0x%02X 提取卡号",
                 card_info->mode_technology);
    }

    return ESP_OK;
}

/* NCI static RF connection 0. Reference: ElectronicCats readerTagCmd,
 * RW_NDEF_T3T and MifareClassic_read_block (NXP-derived implementation).
 * Unlike a control-command exchange, credit notifications can arrive before
 * DATA. Consume them explicitly and reassemble DATA fragments. */
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t bus_handle,
                              const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t capacity, size_t *rx_len)
{
    if (!bus_handle || !tx || !rx || !rx_len || !tx_len || tx_len > 255 ||
        tx_len > s_tag_max_payload) return ESP_ERR_INVALID_ARG;
    *rx_len = 0;
    uint8_t packet[258];
    size_t size;
    bool sent = false;
    TickType_t start = xTaskGetTickCount();
    do {
        if (!sent && s_tag_credits) {
            packet[0] = 0x00; packet[1] = 0x00; packet[2] = (uint8_t)tx_len;
            memcpy(packet + 3, tx, tx_len);
            esp_err_t ret = Pn7150_Send_Nci_Packet(bus_handle, packet, tx_len + 3);
            if (ret != ESP_OK) return ret;
            if (s_tag_credits != 0xFF) --s_tag_credits; /* FF = flow control disabled */
            sent = true;
        }
        esp_err_t ret = Pn7150_Receive_Nci_Packet(bus_handle, packet,
                                                sizeof(packet), &size, 100);
        if (ret == ESP_ERR_TIMEOUT) continue;
        if (ret != ESP_OK) return ret;
        if (size >= 4 && packet[0] == 0x60 && packet[1] == 0x06) {
            ret = tag_credits(packet, size);
            if (ret != ESP_OK) return ret;
        } else if (sent && (packet[0] & 0xEF) == 0x00 && packet[1] == 0x00) {
            if (*rx_len + packet[2] > capacity) return ESP_ERR_INVALID_SIZE;
            memcpy(rx + *rx_len, packet + 3, packet[2]);
            *rx_len += packet[2];
            if (!(packet[0] & 0x10)) return ESP_OK;
        } else if (packet[0] == 0x61 && packet[1] == 0x06) {
            return ESP_ERR_INVALID_STATE;
        } else if (packet[0] == 0x60 && packet[1] == 0x07) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(1500));
    return ESP_ERR_TIMEOUT;
}

/* ElectronicCats readerReActivate: Sleep, then select the actual discovery ID.
 * Used after an unauthenticated Classic read before starting Crypto1 anew. */
esp_err_t Pn7150_ReActivate(i2c_master_bus_handle_t bus_handle,const pn7150_card_info_t *card)
{
    if (!bus_handle || !card) return ESP_ERR_INVALID_ARG;
    const uint8_t sleep[]={0x21,0x06,1,1};uint8_t rx[258];size_t n=0;
    esp_err_t ret=Pn7150_Transceive(bus_handle,sleep,sizeof(sleep),rx,sizeof(rx),&n);
    if (ret!=ESP_OK) return ret;
    if (n!=4 || rx[3]) return ESP_ERR_INVALID_RESPONSE;
    TickType_t start=xTaskGetTickCount();bool slept=false;
    do {
        ret=Pn7150_Receive_Nci_Packet(bus_handle,rx,sizeof(rx),&n,100);
        if (ret==ESP_ERR_TIMEOUT) continue;
        if (ret!=ESP_OK) return ret;
        if (n>=5 && rx[0]==0x61 && rx[1]==6 && rx[3]==1) {slept=true;break;}
    } while (xTaskGetTickCount()-start<pdMS_TO_TICKS(1000));
    if (!slept) return ESP_ERR_TIMEOUT;
    const uint8_t select[]={0x21,4,3,card->discovery_id,card->protocol,card->rf_interface};
    ret=Pn7150_Transceive(bus_handle,select,sizeof(select),rx,sizeof(rx),&n);
    if (ret!=ESP_OK) return ret;
    if (n!=4 || rx[3]) return ESP_ERR_INVALID_RESPONSE;
    pn7150_card_info_t activated;
    ret=Pn7150_Read_Activated_Notification(bus_handle,&activated);
    if (ret!=ESP_OK) return ret==ESP_ERR_TIMEOUT ? ESP_ERR_INVALID_STATE : ret;
    return activated.uid_length==card->uid_length &&
           !memcmp(activated.uid,card->uid,card->uid_length) &&
           activated.protocol==card->protocol && activated.rf_interface==card->rf_interface ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t Pn7150_Wait_Card_Removed(i2c_master_bus_handle_t bus_handle,
                                   const pn7150_card_info_t *card_info)
{
    static const uint8_t iso_dep_presence_check_cmd[] = {0x2F, 0x11, 0x00};
    uint8_t response[3 + 255] = {0};
    size_t response_size = 0;

    if (bus_handle == NULL || card_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (card_info->protocol != 0x04) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (card_info->rf_interface != 0x02) {
        ESP_LOGE(TAG, "ISO-DEP 在场检测需要 interface=0x02，当前为 0x%02X",
                 card_info->rf_interface);
        return ESP_ERR_INVALID_STATE;
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(PN7150_PRESENCE_CHECK_INTERVAL_MS));
        if(interrupted())return ESP_ERR_TIMEOUT;

        esp_err_t ret = Pn7150_Transceive(bus_handle,
                                          iso_dep_presence_check_cmd,
                                          sizeof(iso_dep_presence_check_cmd),
                                          response, sizeof(response),
                                          &response_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ISO-DEP 在场检测命令收发失败: %s",
                     esp_err_to_name(ret));
            return ret;
        }

        bool command_ok = response_size == 4 && response[0] == 0x4F &&
                          response[1] == 0x11 && response[2] == 0x01 &&
                          response[3] == 0x00;
        if (!command_ok) {
            ESP_LOGE(TAG, "ISO-DEP 在场检测命令响应无效");
            ESP_LOG_BUFFER_HEXDUMP(TAG, response, response_size, ESP_LOG_ERROR);
            return ESP_ERR_INVALID_RESPONSE;
        }

        ret = Pn7150_Receive_Nci_Packet(bus_handle, response,
                                        sizeof(response), &response_size,
                                        1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "等待 ISO-DEP 在场检测通知失败: %s",
                     esp_err_to_name(ret));
            return ret;
        }

        bool notification_ok = response_size == 4 &&
                               response[0] == 0x6F &&
                               response[1] == 0x11 &&
                               response[2] == 0x01;
        if (!notification_ok || response[3] > 0x01) {
            ESP_LOGE(TAG, "ISO-DEP 在场检测通知无效");
            ESP_LOG_BUFFER_HEXDUMP(TAG, response, response_size, ESP_LOG_ERROR);
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (response[3] == 0x01) {
            continue;
        }

        /* 6F 11 01 00 is the only notification that means card removed. */
        return ESP_OK;
    }
}

esp_err_t Pn7150_Start_Discovery(i2c_master_bus_handle_t bus_handle)
{
    uint8_t response[3 + 255] = {0};
    size_t response_size = 0;
    esp_err_t ret = Pn7150_Transceive(bus_handle, s_rf_discover_cmd,
                                      sizeof(s_rf_discover_cmd), response,
                                      sizeof(response), &response_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RF_DISCOVER_CMD 收发失败: %s", esp_err_to_name(ret));
        return ret;
    }

    for (unsigned attempt = 0; attempt < 3; attempt++) {
        if (response_size >= 4 && response[0] == 0x41 &&
            response[1] == 0x03) {
            if (response[3] == 0x00 || response[3] == 0xA0) {
                return ESP_OK;
            }

            ESP_LOGE(TAG, "RF_DISCOVER_CMD 状态错误: 0x%02X", response[3]);
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (response_size >= 3 && response[0] == 0x61 &&
            response[1] == 0x06) {
            ESP_LOGD(TAG, "启动发现前收到 RF_DEACTIVATE_NTF");
        } else {
            ESP_LOGW(TAG, "启动发现时跳过 NCI 报文: %02X %02X",
                     response[0], response[1]);
        }

        ret = Pn7150_Receive_Nci_Packet(bus_handle, response,
                                        sizeof(response), &response_size,
                                        1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "等待 RF_DISCOVER_RSP 失败: %s",
                     esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGE(TAG, "未收到 RF_DISCOVER_RSP");
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t Pn7150_Prepare_Next_Card(i2c_master_bus_handle_t bus_handle)
{
    /* Deactivate directly to Discovery, as used by NXP NxpNci_RestartDiscovery. */
    static const uint8_t rf_deactivate_cmd[] = {0x21, 0x06, 0x01, 0x03};
    uint8_t response[3 + 255] = {0};
    size_t response_size = 0;

    /* REJECTED (0x06) does not prove Discovery is running; after failed
     * activation the NFCC may be idle. Explicitly request Discovery. */
    esp_err_t ret = Pn7150_Transceive(bus_handle, rf_deactivate_cmd,
                                      sizeof(rf_deactivate_cmd), response,
                                      sizeof(response), &response_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RF_DEACTIVATE_CMD 收发失败: %s", esp_err_to_name(ret));
        return ret;
    }

    if (response_size < 4 || response[0] != 0x41 || response[1] != 0x06) {
        ESP_LOGE(TAG, "RF_DEACTIVATE_CMD 响应头校验失败");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (response[3] == 0x06) {
        ESP_LOGW(TAG, "RF_DEACTIVATE rejected; verifying RF Discovery");
        return Pn7150_Start_Discovery(bus_handle);
    }

    if (response[3] != 0x00) {
        ESP_LOGE(TAG, "RF_DEACTIVATE_CMD 状态错误: 0x%02X", response[3]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    TickType_t wait_start = xTaskGetTickCount();
    do {
        ret = Pn7150_Receive_Nci_Packet(bus_handle, response, sizeof(response),
                                      &response_size, 100);
        if (ret == ESP_ERR_TIMEOUT) continue;
        if (ret != ESP_OK) break;
        if (response[0] == 0x61 && response[1] == 0x06) break;
        if (response[0] == 0x60 && response[1] == 0x06) {
            ret = tag_credits(response, response_size);
            if (ret != ESP_OK) break;
        }
        ret = ESP_ERR_TIMEOUT;
    } while (xTaskGetTickCount() - wait_start < pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "等待 RF_DEACTIVATE_NTF 失败: %s", esp_err_to_name(ret));
        return ret;
    }
    if (response_size < 5 || response[0] != 0x61 || response[1] != 0x06) {
        ESP_LOGE(TAG, "RF_DEACTIVATE_NTF 校验失败");
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGD(TAG, "RF_DEACTIVATE_NTF: type=0x%02X, reason=0x%02X",
             response[3], response[4]);
    if (response[3] != 0x03) {
        ESP_LOGE(TAG, "RF_DEACTIVATE_NTF 未进入 Discovery: type=0x%02X",
                 response[3]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t Pn7150_Initialize_Card_Emulation(i2c_master_bus_handle_t bus_handle,
                                           const uint8_t uid[4])
{
    if (!bus_handle || !uid) return ESP_ERR_INVALID_ARG;
    static const uint8_t map[] = {0x21,0x00,0x04,0x01,0x04,0x02,0x02};
    static const uint8_t route[] = {0x21,0x01,0x07,0x00,0x01,0x01,0x03,0x00,0x01,0x04};
    static const uint8_t discover[] = {0x21,0x03,0x05,0x02,0x80,0x01,0x82,0x01};
    uint8_t set_uid[] = {0x20,0x02,0x0A,0x02,0x32,0x01,0x20,0x33,0x04,0,0,0,0};
    memcpy(set_uid+9,uid,4);
    esp_err_t ret = Pn7150_Send_Core_Reset(bus_handle);
    if (ret == ESP_OK) ret=Pn7150_Read_Core_Reset_Response(bus_handle);
    if (ret == ESP_OK) ret=Pn7150_Send_Core_Init(bus_handle);
    if (ret == ESP_OK) ret=Pn7150_Read_Core_Init_Response(bus_handle);
    const uint8_t activate[]={0x2F,2,0};
    if(ret==ESP_OK)ret=Pn7150_Check_Command_Response(bus_handle,activate,sizeof(activate),0x4F,2,"CE ACTIVATE",true);
    if(ret==ESP_OK)ret=Pn7150_Configure_Tvdd(bus_handle);
    /* LA_NFCID1 and LA_SEL_RSP take effect after CORE_RESET/INIT. */
    if (ret == ESP_OK) ret=Pn7150_Check_Command_Response(bus_handle,set_uid,sizeof(set_uid),0x40,0x02,"CE LISTEN UID",true);
    if (ret == ESP_OK) {
        ret=Pn7150_Send_Core_Reset(bus_handle);
        if (ret == ESP_OK) ret=Pn7150_Read_Core_Reset_Response(bus_handle);
        if (ret == ESP_OK) ret=Pn7150_Send_Core_Init(bus_handle);
        if (ret == ESP_OK) ret=Pn7150_Read_Core_Init_Response(bus_handle);
    }
    if(ret==ESP_OK)ret=Pn7150_Check_Command_Response(bus_handle,activate,sizeof(activate),0x4F,2,"CE ACTIVATE",true);
    if (ret == ESP_OK) ret=Pn7150_Check_Command_Response(bus_handle,map,sizeof(map),0x41,0x00,"CE RF_DISCOVER_MAP",true);
    if (ret == ESP_OK) ret=Pn7150_Check_Command_Response(bus_handle,route,sizeof(route),0x41,0x01,"CE LISTEN_ROUTING",true);
    if (ret == ESP_OK) ret=Pn7150_Check_Command_Response(bus_handle,discover,sizeof(discover),0x41,0x03,"CE RF_DISCOVER",true);
    if (ret == ESP_OK) {s_tag_credits=0;s_tag_max_payload=0;}
    return ret;
}

esp_err_t Pn7150_Card_Emulation_Receive(i2c_master_bus_handle_t bus_handle,
                                        uint8_t *data,size_t capacity,size_t *length,
                                        uint8_t *kind,uint32_t timeout_ms)
{
    if (!bus_handle || !data || !length || !kind || capacity<3)return ESP_ERR_INVALID_ARG;
    *length=0;*kind=0;
    uint8_t packet[258];size_t size=0;
    TickType_t start=xTaskGetTickCount();bool partial=false;
    do {
        esp_err_t ret=Pn7150_Receive_Nci_Packet(bus_handle,packet,sizeof(packet),&size,partial?1000:timeout_ms);
        if(ret!=ESP_OK)return partial && ret==ESP_ERR_TIMEOUT?ESP_ERR_INVALID_RESPONSE:ret;
        if(packet[0]==0x60 && packet[1]==6){ret=tag_credits(packet,size);if(ret!=ESP_OK)return ret;continue;}
        if(packet[0]==0x61 && packet[1]==5){
            if(size<10 || packet[4]!=2 || packet[5]!=4 || !(packet[6]&0x80) || !packet[7])return ESP_ERR_NOT_SUPPORTED;
            s_tag_max_payload=packet[7];s_tag_credits=packet[8];*kind=1;return ESP_OK;
        }
        if(packet[0]==0x61 && packet[1]==6){
            if(size!=5)return ESP_ERR_INVALID_RESPONSE;
            s_tag_credits=0;s_tag_max_payload=0;
            /* Discovery deactivation already resumes listening. Idle needs
             * Listen discovery; Sleep first returns to Discovery. */
            if(packet[3]==0){
                const uint8_t listen[]={0x21,3,5,2,0x80,1,0x82,1};
                ret=Pn7150_Check_Command_Response(bus_handle,listen,sizeof(listen),0x41,3,"CE RELISTEN",false);
                if(ret!=ESP_OK)return ret;
            }else if(packet[3]==1 || packet[3]==2){
                const uint8_t restart[]={0x21,6,1,3};
                ret=Pn7150_Check_Command_Response(bus_handle,restart,sizeof(restart),0x41,6,"CE RETURN_DISCOVERY",false);
                if(ret!=ESP_OK)return ret;
            }else if(packet[3]!=3){
                return ESP_ERR_INVALID_RESPONSE;
            }
            *kind=3;*length=0;return ESP_OK;
        }
        if((packet[0]&0xEF)==0 && packet[1]==0){
            if(!s_tag_max_payload || *length+packet[2]>capacity)return ESP_ERR_INVALID_SIZE;
            memcpy(data+*length,packet+3,packet[2]);*length+=packet[2];
            partial=true;
            if(!(packet[0]&0x10)){*kind=2;return ESP_OK;}
        }else if(packet[0]==0x60 && packet[1]==7)return ESP_ERR_INVALID_RESPONSE;
    }while(xTaskGetTickCount()-start<pdMS_TO_TICKS(partial?1000:timeout_ms));
    return partial?ESP_ERR_INVALID_RESPONSE:ESP_ERR_TIMEOUT;
}

esp_err_t Pn7150_Card_Emulation_Send(i2c_master_bus_handle_t bus_handle,
                                     const uint8_t *data,size_t length)
{
    if(!bus_handle || !data || !length || length>255 || !s_tag_max_payload)return ESP_ERR_INVALID_ARG;
    uint8_t packet[258];size_t offset=0;
    TickType_t start=xTaskGetTickCount();
    while(offset<length){
        if(!s_tag_credits){
            size_t size=0;esp_err_t ret=Pn7150_Receive_Nci_Packet(bus_handle,packet,sizeof(packet),&size,100);
            if(ret!=ESP_OK && ret!=ESP_ERR_TIMEOUT)return ret;
            if(ret==ESP_OK){
                if(packet[0]!=0x60 || packet[1]!=6)return ESP_ERR_INVALID_STATE;
                ret=tag_credits(packet,size);if(ret!=ESP_OK)return ret;
            }
            if(xTaskGetTickCount()-start>=pdMS_TO_TICKS(1000))return ESP_ERR_TIMEOUT;
            continue;
        }
        size_t count=length-offset;if(count>s_tag_max_payload)count=s_tag_max_payload;
        packet[0]=offset+count<length?0x10:0;packet[1]=0;packet[2]=(uint8_t)count;
        memcpy(packet+3,data+offset,count);
        esp_err_t ret=Pn7150_Send_Nci_Packet(bus_handle,packet,count+3);if(ret!=ESP_OK)return ret;
        if(s_tag_credits!=0xFF)--s_tag_credits;
        offset+=count;
    }
    return ESP_OK;
}
