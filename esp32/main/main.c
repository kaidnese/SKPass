#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "skpass_usb.h"
#include "pn7150.h"
#include "skpass_cdc.h"
#include "aime_reader.h"
#include "card_writer.h"
#include "uf2_update.h"
#include "rom_download.h"

#define TAG "I2C_MAIN"
#define SCAN_INTERVAL_MS 2000

static i2c_master_bus_handle_t s_bus_handle;
/* NFC task owns this scratch document; don't put the history on its stack. */
static smartcard_info_t s_smartcard;
static card_emu_t s_emulation;
static bool s_listen_mode;
static bool nfc_interrupted(void){return SKPass_CDC_CE_Pending() || Uf2_Update_Is_Active() || Rom_Download_Is_Requested();}

static void card_emulation_loop(void)
{
    uint8_t apdu[261],response[CARD_EMU_MAX_NDEF+4];
    while(!nfc_interrupted()){
        size_t length=0;uint8_t kind=0;
        esp_err_t ret=Pn7150_Card_Emulation_Receive(s_bus_handle,apdu,sizeof(apdu),&length,&kind,100);
        if(ret==ESP_ERR_TIMEOUT)continue;
        if(ret!=ESP_OK){
            const char *error=esp_err_to_name(ret);
            SKPass_CDC_CE_Event(CE_EVT_ERROR,(const uint8_t *)error,strlen(error));return;
        }
        if(kind==1 || kind==3){
            CardEmu_Reset_Session(&s_emulation);
            SKPass_CDC_CE_Event(kind==1?CE_EVT_ACTIVATED:CE_EVT_DEACTIVATED,NULL,0);
        }else if(kind==2){
            bool updated=false;
            SKPass_CDC_CE_Event(CE_EVT_RX,apdu,length);
            size_t count=CardEmu_Process_Apdu(&s_emulation,apdu,length,response,sizeof(response),&updated);
            ret=Pn7150_Card_Emulation_Send(s_bus_handle,response,count);
            if(ret!=ESP_OK){
                const char *error=esp_err_to_name(ret);
                SKPass_CDC_CE_Event(CE_EVT_ERROR,(const uint8_t *)error,strlen(error));return;
            }
            SKPass_CDC_CE_Event(CE_EVT_TX,response,count);
            if(updated)SKPass_CDC_CE_Event(CE_EVT_NDEF,s_emulation.config.ndef,s_emulation.config.ndef_len);
        }
    }
}

static esp_err_t i2c_init(void)
{
    const i2c_master_bus_config_t config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false, /* external pull-ups */
    };

    esp_err_t ret = i2c_new_master_bus(&config, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 主机总线初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C 主机总线初始化成功 (SCL: GPIO%d, SDA: GPIO%d, %u Hz, 外部上拉)",
             I2C_SCL_GPIO, I2C_SDA_GPIO, (unsigned)I2C_MASTER_FREQ_HZ);
    return ESP_OK;
}

static void log_card(const pn7150_card_info_t *card_info)
{
    char uid_text[PN7150_NFCID_MAX_LENGTH * 3] = {0};

    for (size_t index = 0; index < card_info->uid_length; ++index) {
        snprintf(uid_text + index * 3, sizeof(uid_text) - index * 3,
                 index + 1 == card_info->uid_length ? "%02X" : "%02X ",
                 (unsigned)card_info->uid[index]);
    }

    ESP_LOGI(TAG,
             "CARD_ID (mode=0x%02X, protocol=0x%02X, interface=0x%02X): %s",
             card_info->mode_technology, card_info->protocol,
             card_info->rf_interface, uid_text);
}

static esp_err_t restart_discovery_after_card(i2c_master_bus_handle_t bus,
                                              const pn7150_card_info_t *card)
{
    esp_err_t ret = Pn7150_Wait_Card_Removed(bus, card);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CARD_REMOVED");
        SKPass_CDC_Removed();
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        /* Non-ISO-DEP tags are returned to Discovery immediately after the
         * activation notification; no presence command is available for
         * those protocols. */
        ESP_LOGD(TAG, "当前协议没有 ISO-DEP 在场检测，直接重启 RF 发现");
    } else {
        ESP_LOGW(TAG, "卡片在场检测失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = Pn7150_Prepare_Next_Card(bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "重新启动 RF 发现失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "RF 发现已重新启动，等待下一次卡片激活");
    }
    return ret;
}

static void i2c_task(void *arg)
{
    (void)arg;

    if (i2c_init() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    Pn7150_Set_Interrupt_Check(nfc_interrupted);

    while (1) {
        if (Uf2_Update_Is_Active() || Rom_Download_Is_Requested()) {
            SKPass_CDC_Set_Nfc_State(SKPASS_NFC_UPDATE);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        SKPass_CDC_Set_Nfc_State(SKPASS_NFC_INITIALIZING);
        SKPass_CDC_Advanced_Clear();
        ce_request_t ce_request;
        bool switching=SKPass_CDC_CE_Take(&ce_request);
        if(switching){
            s_listen_mode=ce_request.action==CE_APPLY;
            if(s_listen_mode)CardEmu_Set_Config(&s_emulation,&ce_request.config);
            SKPass_CDC_Removed();
        }
        Pn7150_Reset();
        if(s_listen_mode){
            esp_err_t result=Pn7150_Initialize_Card_Emulation(s_bus_handle,s_emulation.config.uid);
            if(switching)SKPass_CDC_CE_Result(ce_request.id,result==ESP_OK?CE_OK:CE_FAILED,
                result==ESP_OK?CE_MODE_LISTEN:CE_MODE_FAULT,s_emulation.config.uid,(uint16_t)result);
            else SKPass_CDC_CE_Result(0,result==ESP_OK?CE_OK:CE_FAILED,
                result==ESP_OK?CE_MODE_LISTEN:CE_MODE_FAULT,s_emulation.config.uid,(uint16_t)result);
            if(result==ESP_OK){
                SKPass_CDC_Set_Nfc_State(SKPASS_NFC_CARD_EMULATION);
                CardEmu_Reset_Session(&s_emulation);card_emulation_loop();
            }
            if(!nfc_interrupted())vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        SKPass_USB_Debug("NFC_RECOVERY: hardware reset; initializing PN7150");
        ESP_LOGI(TAG, "=== PN7150 NCI 初始化 ===");
        esp_err_t initialized=Pn7150_Test_ACK(s_bus_handle)?Pn7150_Initialize(s_bus_handle):ESP_FAIL;
        if(switching)SKPass_CDC_CE_Result(ce_request.id,initialized==ESP_OK?CE_OK:CE_FAILED,
            initialized==ESP_OK?CE_MODE_RW:CE_MODE_FAULT,NULL,(uint16_t)initialized);
        else if(SKPass_CDC_CE_Active())SKPass_CDC_CE_Result(0,initialized==ESP_OK?CE_OK:CE_FAILED,
            initialized==ESP_OK?CE_MODE_RW:CE_MODE_FAULT,NULL,(uint16_t)initialized);
        if (initialized == ESP_OK) {
            ESP_LOGI(TAG, "=== PN7150 开始等待卡片 ===");
            SKPass_USB_Debug("NFC_READY: RF Discovery initialized");
            SKPass_CDC_Set_Nfc_State(SKPASS_NFC_READY);

            while (1) {
                if(SKPass_CDC_CE_Pending())break;
                if (Uf2_Update_Is_Active() || Rom_Download_Is_Requested()) {
                    SKPass_CDC_Set_Nfc_State(SKPASS_NFC_UPDATE);
                    ESP_LOGW(TAG, "UF2 更新模式已启用，停止 PN7150 任务；请重新插拔设备");
                    break;
                }
                SKPass_CDC_Check_Write_Timeout();
                pn7150_card_info_t card_info;
                esp_err_t ret = Pn7150_Read_Activated_Notification(
                    s_bus_handle, &card_info);

                if (ret == ESP_ERR_TIMEOUT) {
                    /* A timeout is the normal no-card state.  Keep the
                     * controller initialized and wait for the next IRQ. */
                    continue;
                }
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "读取卡片失败，重新初始化 PN7150: %s",
                             esp_err_to_name(ret));
                    SKPass_USB_Debug("NFC_RECOVERY: activation or NCI receive failed");
                    break;
                }

                if (Rom_Download_Is_Requested()) break;

                if (card_info.uid_length == 0) {
                    ESP_LOGW(TAG, "PN7150 激活通知未提供可用卡片 ID");
                } else {
                    log_card(&card_info);
                    /* Every activation is submitted.  The SKPass component
                     * applies the reference 3000 ms global suppression, so
                     * the same tag can be scanned again without UID-based
                     * deduplication preventing future reports. */
                    ret = SKPass_USB_SendCard(&card_info);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "SKPass 报告未排队: %s",
                                 esp_err_to_name(ret));
                    }
                }

                bool probe_session_failed = false;
                bool advanced = SKPass_CDC_Advanced_Pending();
                writer_request_t request;
                if(advanced){
                    /* Re-probe the just activated physical card, never trust
                     * a host subtype or cached capacity when executing writes. */
                    SKPass_CDC_Set_Nfc_State(SKPASS_NFC_READING);
                    SKPass_CDC_Advanced_Run(s_bus_handle,&card_info);
                    probe_session_failed=true;
                } else if (SKPass_CDC_Take_Write(&card_info,&request)) {
                    SKPass_CDC_Set_Nfc_State(SKPASS_NFC_READING);
                    writer_result_t result;
                    CardWriter_Write(s_bus_handle,&card_info,&request,&result);
                    SKPass_CDC_Advanced_Clear();
                    SKPass_CDC_Write_Result(&result);
                    ESP_LOGI(TAG,"WRITE id=%lu status=%u stage=%u detail=%02X",
                             (unsigned long)result.id,result.status,result.stage,result.detail);
                    if (result.status != WRITE_SUCCESS) {
                        SKPass_USB_Debug("NFC_RECOVERY: write failed; discarding RF session");
                        break;
                    }
                } else if (SKPass_CDC_Should_Read()) {
                    SKPass_CDC_Set_Nfc_State(SKPASS_NFC_READING);
                    char physical_uid[PN7150_NFCID_MAX_LENGTH * 2 + 1] = {0};
                    for (size_t x = 0; x < card_info.uid_length; ++x)
                        snprintf(physical_uid + x * 2, sizeof(physical_uid) - x * 2, "%02X", card_info.uid[x]);
                    char tag_text[128];
                    snprintf(tag_text, sizeof(tag_text), "TAG uid=%s mode=%02X protocol=%02X interface=%02X sak=%02X",
                             physical_uid, card_info.mode_technology, card_info.protocol,
                             card_info.rf_interface, card_info.sak_valid ? card_info.sak : 0xFF);
                    SKPass_USB_Debug(tag_text);
                    aime_card_event_t event = {0};
                    bool known_arcade = card_info.protocol == 3 && card_info.system_code_valid &&
                        ((card_info.system_code[0] == 0x88 && card_info.system_code[1] == 0xB4) ||
                         (card_info.system_code[0] == 0xFE && card_info.system_code[1] == 0));
                    /* Lite/Lite-S activation may omit its system code. Give
                     * the existing reader priority for those IC families;
                     * this is an order hint, not proof of an arcade issuer. */
                    bool arcade_first = known_arcade || (card_info.protocol == 3 && card_info.pmm_valid &&
                        (card_info.pmm[1] == 0xF0 || card_info.pmm[1] == 0xF1));
                    /* Prioritize the existing arcade path. Probes must never
                     * prevent a known Amusement IC or Classic read. */
                    if (card_info.protocol == 0x80 || arcade_first) {
                        Aime_Read_Card(s_bus_handle, &card_info, &event);
                        SKPass_CDC_Card(&event);
                        if (arcade_first && event.status == 0)
                            SmartCard_Probe(s_bus_handle, &card_info, &s_smartcard);
                        else SmartCard_Describe(&card_info, &s_smartcard);
                    } else {
                        SmartCard_Probe(s_bus_handle, &card_info, &s_smartcard);
                        bool transit = strncmp(s_smartcard.kind, "transit_", 8) == 0;
                        if (!transit && !s_smartcard.session_failed && card_info.protocol == 3) {
                            Aime_Read_Card(s_bus_handle, &card_info, &event);
                        } else {
                            /* Public NFC applications use an independent
                             * document; don't derive arcade codes from them. */
                            memcpy(event.idm, card_info.uid, card_info.uid_length < 8 ? card_info.uid_length : 8);
                            event.card_type = card_info.protocol == 6 ? 0x30 : 0;
                            event.status = 0x16;
                        }
                        SKPass_CDC_Card(&event);
                    }
                    SKPass_CDC_SmartCard(&s_smartcard);
                    probe_session_failed = s_smartcard.session_failed;
                    if (event.card_type == 0x10 && event.status != 0) {
                        /* Geometry remains valid for explicit authenticated
                         * advanced operations with a user-selected key. */
                        SmartCard_Describe(&card_info,&s_smartcard);
                        SKPass_CDC_Advanced_Card(s_bus_handle,&card_info,&s_smartcard);
                        /* A failed Classic exchange may leave RF in Sleep,
                         * Idle or waiting for a lost credit. Discard it. */
                        SKPass_USB_Debug("NFC_RECOVERY: MIFARE read failed; discarding RF session");
                        /* Explicit advanced authentication may use another
                         * key. Recover Discovery and keep its fresh identity
                         * lease; don't make the controls vanish every retry. */
                        ret=Pn7150_Prepare_Next_Card(s_bus_handle);
                        if(ret!=ESP_OK)break;
                        SKPass_CDC_Set_Nfc_State(SKPASS_NFC_READY);
                        continue;
                    }
                    SKPass_CDC_Advanced_Card(s_bus_handle,&card_info,&s_smartcard);
                } else {
                    /* Refresh the lease on actual activation only. Reuse
                     * capacity solely for an identical freshly activated UID. */
                    char uid[21]={0};
                    for(size_t x=0;x<card_info.uid_length && x<10;x++)snprintf(uid+x*2,3,"%02X",card_info.uid[x]);
                    if(strcmp(uid,s_smartcard.uid) || card_info.protocol!=s_smartcard.protocol)
                        SmartCard_Probe(s_bus_handle,&card_info,&s_smartcard);
                    SKPass_CDC_Advanced_Card(s_bus_handle,&card_info,&s_smartcard);
                }

                /* A missing public application/service is not a controller
                 * fault. On transport failure first recover Discovery in NCI;
                 * only a failed RF restart needs the existing hardware reset. */
                ret = probe_session_failed ? Pn7150_Prepare_Next_Card(s_bus_handle) :
                      restart_discovery_after_card(s_bus_handle, &card_info);
                if (ret != ESP_OK) {
                    SKPass_USB_Debug("NFC_RECOVERY: RF restart failed");
                    break;
                }
                SKPass_CDC_Set_Nfc_State(SKPASS_NFC_READY);
            }
        }

        if(SKPass_CDC_CE_Pending())continue;
        SKPass_CDC_Set_Nfc_State((Uf2_Update_Is_Active() || Rom_Download_Is_Requested()) ? SKPASS_NFC_UPDATE : SKPASS_NFC_RECOVERING);
        ESP_LOGW(TAG, "PN7150 初始化失败，%d ms 后重试", SCAN_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));
    }
}

void app_main(void)
{
    Pn7150_Init();

    /* A CDC update command requests a one-shot reboot into UF2 MSC mode. */
    bool uf2_mode = Uf2_Update_Boot();
    SKPass_CDC_Set_Nfc_State(uf2_mode ? SKPASS_NFC_UPDATE : SKPASS_NFC_INITIALIZING);
    if (uf2_mode) gpio_set_level(GPIO_VEN_PIN, 0);

    esp_err_t ret = SKPass_USB_Init();
    if (ret != ESP_OK) {
        /* Keep the NFC reader usable for diagnostics even if USB installation
         * fails; a later hardware/host reset can then be investigated from
         * the UART log. */
        ESP_LOGE(TAG, "SKPass USB 初始化失败: %s", esp_err_to_name(ret));
    }

    if (!uf2_mode) {
        xTaskCreatePinnedToCore(i2c_task, "IIC_task", 6144, NULL, 3, NULL, 1);
    } else {
        ESP_LOGW(TAG, "UF2 MSC 更新模式：PN7150 读卡任务已禁用");
    }
}
