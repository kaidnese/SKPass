#ifndef SKPASS_ADVANCED_READER_H
#define SKPASS_ADVANCED_READER_H
#include "smartcard_probe.h"
#include "advanced_protocol.h"
void Advanced_Describe(const pn7150_card_info_t *,const smartcard_info_t *,advanced_target_t *);
/* NFC task only. No transport callback may issue RF commands. */
void Advanced_Execute(i2c_master_bus_handle_t,const pn7150_card_info_t *,
                      const advanced_target_t *,const advanced_request_t *,advanced_result_t *);
#endif
