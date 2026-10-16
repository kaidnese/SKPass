#ifndef SKPASS_CDC_H
#define SKPASS_CDC_H
#include "esp_err.h"
#include "aime_reader.h"
#include "smartcard_probe.h"
#include "writer_protocol.h"
#include "advanced_reader.h"
#include "ce_protocol.h"
esp_err_t SKPass_CDC_Init(void);
/* Called from TinyUSB's callback; only copies a report to the I/O queue. */
void SKPass_HID_Receive(const uint8_t report[64]);
typedef enum {
    SKPASS_NFC_INITIALIZING, SKPASS_NFC_READY, SKPASS_NFC_READING,
    SKPASS_NFC_RECOVERING, SKPASS_NFC_UPDATE, SKPASS_NFC_CARD_EMULATION
} skpass_nfc_state_t;
void SKPass_CDC_Set_Nfc_State(skpass_nfc_state_t state);
bool SKPass_CDC_Should_Read(void);
void SKPass_CDC_Card(const aime_card_event_t *event);
void SKPass_CDC_SmartCard(const smartcard_info_t *info);
void SKPass_CDC_Removed(void);
void SKPass_CDC_Check_Write_Timeout(void);
bool SKPass_CDC_Take_Write(const pn7150_card_info_t *card, writer_request_t *request);
void SKPass_CDC_Write_Result(const writer_result_t *result);
bool SKPass_CDC_Advanced_Pending(void);
/* Claim a queued operation before re-probing; called only on the NFC task. */
void SKPass_CDC_Advanced_Run(i2c_master_bus_handle_t bus,const pn7150_card_info_t *);
void SKPass_CDC_Advanced_Clear(void);
void SKPass_CDC_Advanced_Card(i2c_master_bus_handle_t bus,const pn7150_card_info_t *,const smartcard_info_t *);

/* Card-emulation control is queued by the USB task and consumed by the NFC
 * task. PN7150 I/O never runs from a USB callback. */
bool SKPass_CDC_CE_Take(ce_request_t *request);
void SKPass_CDC_CE_Result(uint32_t id, uint8_t status, uint8_t mode,
                          const uint8_t uid[4], uint16_t error);
bool SKPass_CDC_CE_Active(void);
bool SKPass_CDC_CE_Pending(void);
const card_emu_config_t *SKPass_CDC_CE_Config(void);
void SKPass_CDC_CE_Event(uint8_t type, const uint8_t *data, size_t length);
#endif
