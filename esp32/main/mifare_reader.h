#ifndef MIFARE_READER_H
#define MIFARE_READER_H
#include "pn7150.h"

/* Sector 0 data blocks only. No writes or sector-trailer reads. */
bool Mifare_Read_Unauthenticated(i2c_master_bus_handle_t bus, uint8_t out[48]);
/* Existing CDC statuses: 0=success, 12=authentication, 13=read/session failure. */
uint8_t Mifare_Read_Authenticated(i2c_master_bus_handle_t bus,
    const pn7150_card_info_t *card, const uint8_t key[6],
    const char *key_slot, uint8_t out[48]);
#endif
