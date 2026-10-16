#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 7
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *config, i2c_master_dev_handle_t *device);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
    const uint8_t *data, size_t size, int timeout);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t device,
    uint8_t *data, size_t size, int timeout);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout);
