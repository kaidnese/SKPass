#ifndef WRITER_PROTOCOL_H
#define WRITER_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WRITER_REQUEST_BYTES 32
#define WRITER_RESULT_BYTES 51
enum { WRITE_ACCEPTED=1, WRITE_EXPIRED, WRITE_REJECTED, WRITE_BUSY,
       WRITE_SUCCESS, WRITE_AUTH_FAILED, WRITE_PREFLIGHT_FAILED,
       WRITE_FAILED, WRITE_VERIFY_FAILED, WRITE_DISCONNECTED };
typedef struct {
    uint32_t id;
    uint8_t uid_len, uid[10], key[6], bcd[10], options;
} writer_request_t;
typedef struct {
    uint32_t id;
    uint8_t status, stage, detail, uid_len, uid[10], verified_mask, blocks[32];
} writer_result_t;
bool writer_decode_request(const uint8_t *data, size_t size, writer_request_t *out);
size_t writer_encode_result(const writer_result_t *result, uint8_t out[WRITER_RESULT_BYTES]);
void writer_build_blocks(const writer_request_t *request, uint8_t out[32]);
#ifdef __cplusplus
}
#endif
#endif
