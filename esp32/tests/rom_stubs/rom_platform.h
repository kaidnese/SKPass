#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PN7150_H
#define GPIO_VEN_PIN 6
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_SUPPORTED 0x106
typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_EFUSE_DIS_DOWNLOAD_MODE 1
#define ESP_EFUSE_DIS_FORCE_DOWNLOAD 2
#define RTC_CNTL_OPTION1_REG 0x6000812C
#define RTC_CNTL_FORCE_DOWNLOAD_BOOT 1
#define REG_SET_BIT(reg,bit) mock_set_bit(reg,bit)
bool esp_efuse_read_field_bit(int field);
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
void vTaskDelay(TickType_t);
void gpio_set_level(int pin,int level);
void tud_disconnect(void);
void mock_set_bit(uint32_t reg,uint32_t bit);
void esp_rom_software_reset_system(void);
