#include "rom_download.h"
#include "pn7150.h"
#include "driver/gpio.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_rom_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"
#include <stdatomic.h>

static atomic_bool s_requested;

bool Rom_Download_Is_Requested(void) { return atomic_load_explicit(&s_requested, memory_order_relaxed); }

static void enter_task(void *arg)
{
    (void)arg;
    /* The I/O task must have time to deliver B4 before the USB disconnect. */
    vTaskDelay(pdMS_TO_TICKS(600));
    gpio_set_level(GPIO_VEN_PIN, 0);
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    /* ESP-IDF usb_console.c uses this RTC bit for ROM download. A digital
     * system reset also resets the TinyUSB PHY/mux; RTC OPTION1 is retained.
     * The host downloads without an automatic reset; unplug/replug USB
     * returns to the application after the update. */
    REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_rom_software_reset_system();
    for (;;) vTaskDelay(portMAX_DELAY);
}

esp_err_t Rom_Download_Enter(void)
{
    if (Rom_Download_Is_Requested()) return ESP_ERR_INVALID_STATE;
    if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE) ||
        esp_efuse_read_field_bit(ESP_EFUSE_DIS_FORCE_DOWNLOAD))
        return ESP_ERR_NOT_SUPPORTED;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_requested, &expected, true)) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(enter_task, "rom_download", 2048, NULL, 4, NULL) != pdPASS) {
        atomic_store(&s_requested, false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
