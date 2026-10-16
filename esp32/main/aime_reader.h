#ifndef AIME_READER_H
#define AIME_READER_H
#include "pn7150.h"
typedef struct {
    uint8_t card_type, status;
    uint8_t idm[8], pmm[8];
    uint8_t spad_len, spad[16];
    uint8_t mifare_len, mifare[48];
} aime_card_event_t;
void Aime_Read_Card(i2c_master_bus_handle_t bus, const pn7150_card_info_t *card,
                    aime_card_event_t *event);
/* Wire encoding is explicit; never transmit a C struct with padding. */
size_t Aime_Encode_Event(const aime_card_event_t *event, uint8_t out[84]);
#endif
