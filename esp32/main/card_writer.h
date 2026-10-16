#ifndef CARD_WRITER_H
#define CARD_WRITER_H
#include "pn7150.h"
#include "writer_protocol.h"
/* NFC task only. No arbitrary block-write API is exposed. */
void CardWriter_Write(i2c_master_bus_handle_t bus, const pn7150_card_info_t *card,
                      const writer_request_t *request, writer_result_t *result);
#endif
