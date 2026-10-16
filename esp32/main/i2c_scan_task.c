#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "I2C_SCAN";

#define I2C_MASTER_SCL_IO           22      // SCL GPIO 引脚
#define I2C_MASTER_SDA_IO           21      // SDA GPIO 引脚
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000  // 100 kHz
#define SCAN_INTERVAL_MS            2000    // 扫描间隔 2 秒

// I2C 地址扫描范围（7位地址）
#define I2C_ADDR_START              0x08    // 起始地址（0x00-0x07 为保留地址）
#define I2C_ADDR_END                0x77    // 结束地址（0x78-0x7F 为保留地址）

static i2c_master_bus_handle_t bus_handle = NULL;

/**
 * @brief 初始化 I2C 主机总线
 */
static esp_err_t i2c_master_init(void)
{
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  // 启用内部上拉
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 主机总线初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C 主机总线初始化成功 (SCL: GPIO%d, SDA: GPIO%d)",
             I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO);
    return ESP_OK;
}

/**
 * @brief 扫描 I2C 总线上的所有设备
 */
static void i2c_scan_devices(void)
{
    uint8_t found_count = 0;
    esp_err_t ret;

    ESP_LOGI(TAG, "开始扫描 I2C 总线...");
    ESP_LOGI(TAG, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

    for (uint8_t addr = 0x00; addr < 0x80; addr++) {
        // 每行开始打印地址前缀
        if (addr % 16 == 0) {
            printf("%02x: ", addr);
        }

        // 跳过保留地址
        if (addr < I2C_ADDR_START || addr > I2C_ADDR_END) {
            printf("   ");
        } else {
            // 探测设备
            ret = i2c_master_probe(bus_handle, addr, 50);  // 50ms 超时

            if (ret == ESP_OK) {
                printf("%02x ", addr);
                found_count++;
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("-- ");
            } else {
                printf("-- ");
            }
        }

        // 每行结束换行
        if ((addr + 1) % 16 == 0) {
            printf("\n");
        }
    }

    if (found_count > 0) {
        ESP_LOGI(TAG, "扫描完成，共找到 %d 个 I2C 设备", found_count);
    } else {
        ESP_LOGW(TAG, "扫描完成，未找到任何 I2C 设备");
    }

    printf("\n");
}

/**
 * @brief I2C 扫描任务
 */
void i2c_scan_task(void *pvParameters)
{
    // 初始化 I2C 主机总线
    if (i2c_master_init() != ESP_OK) {
        ESP_LOGE(TAG, "无法启动 I2C 扫描任务");
        vTaskDelete(NULL);
        return;
    }

    // 延迟一下，等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(500));

    // 持续扫描循环
    while (1) {
        i2c_scan_devices();
        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));
    }
}
