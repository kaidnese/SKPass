#ifndef READER_FRAME_H
#define READER_FRAME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define READER_MAX_PAYLOAD 512
#define READER_MAX_FRAME (READER_MAX_PAYLOAD + 8)
uint16_t reader_crc16(const uint8_t *data, size_t size);
size_t reader_encode_frame(uint8_t msg, const uint8_t *payload, size_t size,
                           uint8_t *out, size_t capacity);
typedef struct { size_t size; uint8_t data[READER_MAX_FRAME]; } reader_decoder_t;
typedef void (*reader_frame_callback_t)(uint8_t msg, const uint8_t *payload, size_t size, void *context);
void reader_decoder_feed(reader_decoder_t *decoder, const uint8_t *data, size_t size,
                         reader_frame_callback_t callback, void *context);
#ifdef __cplusplus
}
#endif
#endif
