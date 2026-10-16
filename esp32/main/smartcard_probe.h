#ifndef SMARTCARD_PROBE_H
#define SMARTCARD_PROBE_H
#include "pn7150.h"

#define SMARTCARD_MAX_RECORDS 10
#define SMARTCARD_DETAIL_MSG 0x12
typedef struct {
    char date[15], raw[47];
    uint8_t type;
    uint16_t entry, exit;
    int32_t amount, balance;
    bool has_amount, has_balance;
} smartcard_record_t;
typedef struct {
    uint32_t scan;
    char uid[21], kind[24], number[25], issuer[17], start[9], expiry[9];
    char label[33], warning[48], currency[4];
    int32_t balance;
    uint16_t blocks, block_size, pages, first_page, last_page, sectors, sw;
    uint32_t raw_balance;
    uint8_t records;
    bool has_balance, estimated, session_failed, recognized, arcade, lite_s_verified, t2_writable;
    uint8_t protocol, rf_interface;
    smartcard_record_t history[SMARTCARD_MAX_RECORDS];
} smartcard_info_t;

/* Read-only bounded probes. No authentication, payment, write or emulation.
 * Call on the NFC task only. Exact activated UID is retained in every part. */
void SmartCard_Probe(i2c_master_bus_handle_t bus, const pn7150_card_info_t *card,
                     smartcard_info_t *info);
/* Metadata only: safe even when the preceding arcade read lost its RF session. */
void SmartCard_Describe(const pn7150_card_info_t *card, smartcard_info_t *info);
/* One summary (part 0) and up to ten records. Each UTF-8 JSON <= 512 bytes,
 * so existing CDC framing / HID fragmentation limits stay unchanged. */
size_t SmartCard_Encode_Part(const smartcard_info_t *info, unsigned part,
                            char *out, size_t capacity);
#endif
