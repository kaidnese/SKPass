#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NO_MEM 0x101
#define RTC_NOINIT_ATTR
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
typedef struct {uint32_t size;} esp_partition_t;
typedef struct {char project_name[32];} esp_app_desc_t;
typedef unsigned esp_ota_handle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef enum {eSetBits} eNotifyAction;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define ESP_RST_SW 3
#define SCSI_SENSE_ILLEGAL_REQUEST 5
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(TickType_t);
int xTaskNotify(TaskHandle_t,uint32_t,eNotifyAction);
int xTaskNotifyWait(uint32_t,uint32_t,uint32_t *,TickType_t);
int esp_reset_reason(void);
void esp_restart(void);
esp_err_t esp_flash_get_size(void *,uint32_t *);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_begin(const esp_partition_t *,size_t,esp_ota_handle_t *);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_write_with_offset(esp_ota_handle_t,const void *,size_t,uint32_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *,esp_app_desc_t *);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_partition_read(const esp_partition_t *,uint32_t,void *,size_t);
bool tud_disconnect(void);
void tud_msc_set_sense(uint8_t,uint8_t,uint8_t,uint8_t);
