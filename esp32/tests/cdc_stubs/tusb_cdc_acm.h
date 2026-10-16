#pragma once
#include "esp_err.h"
#define CDC_EVENT_LINE_STATE_CHANGED 1
#define TINYUSB_USBDEV_0 0
#define TINYUSB_CDC_ACM_0 0
typedef struct {int type;struct {int dtr;} line_state_changed_data;} cdcacm_event_t;
typedef struct {int usb_dev,cdc_port;void (*callback_line_state_changed)(int,cdcacm_event_t *);} tinyusb_config_cdcacm_t;
esp_err_t tusb_cdc_acm_init(const tinyusb_config_cdcacm_t *);
