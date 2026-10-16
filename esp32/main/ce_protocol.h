#ifndef SKPASS_CE_PROTOCOL_H
#define SKPASS_CE_PROTOCOL_H
#include "card_emulation.h"
#define CE_REQUEST_MSG 0x23
#define CE_RESPONSE_MSG 0x93
#define CE_EVENT_MSG 0x14
#define CE_REPLY_BYTES 12
#define CE_EVENT_HEADER 10
enum {CE_QUERY=0,CE_READER=1,CE_APPLY=2};
enum {CE_OK=0,CE_ACCEPTED=1,CE_INVALID=2,CE_BUSY=3,CE_FAILED=4,CE_DISCONNECTED=5};
enum {CE_MODE_RW=0,CE_MODE_LISTEN=1,CE_MODE_PENDING=2,CE_MODE_FAULT=3};
enum {CE_EVT_ACTIVATED=1,CE_EVT_DEACTIVATED=2,CE_EVT_RX=3,CE_EVT_TX=4,CE_EVT_NDEF=5,CE_EVT_ERROR=6};
typedef struct {uint32_t id;uint8_t action;card_emu_config_t config;bool hid;uint32_t generation,queued_ms;} ce_request_t;
bool CE_Decode_Request(const uint8_t *,size_t,ce_request_t *);
size_t CE_Encode_Request(const ce_request_t *,uint8_t *,size_t);
void CE_Encode_Reply(uint32_t id,uint8_t status,uint8_t mode,const uint8_t uid[4],uint16_t error,uint8_t out[CE_REPLY_BYTES]);
#endif
