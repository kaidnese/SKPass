#ifndef WRITER_TEST_PLATFORM_H
#define WRITER_TEST_PLATFORM_H
#define PN7150_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef void *i2c_master_bus_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
typedef struct {
    uint8_t discovery_id,rf_interface,protocol,mode_technology,uid[10];
    size_t uid_length;
    uint8_t sak;bool sak_valid;
} pn7150_card_info_t;
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t,const uint8_t *,size_t,uint8_t *,size_t,size_t *);
#endif
