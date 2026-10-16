#ifndef SKPASS_ADVANCED_PROTOCOL_H
#define SKPASS_ADVANCED_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ADV_REQUEST_MSG 0x22
#define ADV_RESPONSE_MSG 0x92
#define ADV_TARGET_MSG 0x13
#define ADV_MAX_DATA 256
#define ADV_TARGET_BYTES 23
#define ADV_HEADER_BYTES 36
#define ADV_RESULT_HEADER 30
enum { ADV_NONE, ADV_CLASSIC_1K, ADV_CLASSIC_4K, ADV_CLASSIC_MINI,
       ADV_NTAG213, ADV_NTAG215, ADV_NTAG216, ADV_ULTRALIGHT,
       ADV_LITE_S, ADV_ISO15693_4 };
enum { ADV_READ=1, ADV_WRITE, ADV_UID_WRITE, ADV_NDEF, ADV_SCAN };
enum { ADV_OK, ADV_ACCEPTED, STATUS_CARD_MISMATCH, ADV_INVALID,
       ADV_BUSY, ADV_AUTH_FAILED, ADV_RF_FAILED, ADV_VERIFY_FAILED,
       ADV_PROTECTED, ADV_DISCONNECTED, ADV_DUPLICATE };
enum { ADV_CONFIRM_TRAILER=1, ADV_CONFIRM_GEN2=2, ADV_CONFIRM_UID=4 };
typedef struct {
    uint32_t token;
    uint8_t uid_len, uid[10], protocol, subtype;
    uint16_t first, last;
    uint8_t unit, available;
} advanced_target_t;
typedef struct {
    uint32_t id;
    advanced_target_t target;
    uint8_t operation, flags, key_type, key[6];
    uint16_t address, count, length;
    uint8_t data[ADV_MAX_DATA];
} advanced_request_t;
typedef struct {
    uint32_t id;
    advanced_target_t target;
    uint8_t operation, status, detail;
    uint16_t address, completed, length;
    uint8_t data[ADV_MAX_DATA];
} advanced_result_t;
bool Advanced_Target_Equal(const advanced_target_t *,const advanced_target_t *);
void Advanced_Encode_Target(const advanced_target_t *,uint8_t out[ADV_TARGET_BYTES]);
bool Advanced_Decode_Request(const uint8_t *,size_t,advanced_request_t *);
uint8_t Advanced_Validate(const advanced_target_t *,const advanced_request_t *);
size_t Advanced_Encode_Result(const advanced_result_t *,uint8_t *,size_t);
#endif
