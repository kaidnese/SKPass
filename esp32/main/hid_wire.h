#ifndef SKPASS_HID_WIRE_H
#define SKPASS_HID_WIRE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SKPASS_HID_USAGE_PAGE 0xFFCBu
#define SKPASS_HID_USAGE 1u
#define SKPASS_HID_REPORT_ID 1u
#define SKPASS_HID_REPORT_BYTES 64u
#define SKPASS_HID_CHUNK_BYTES 58u
#define SKPASS_HID_FRAME_BYTES 520u
#define SKPASS_HID_HELLO 0x32u
#define SKPASS_HID_CLOSE 0x33u
#define SKPASS_HID_HELLO_REPLY 0xB2u
typedef struct {
    uint8_t data[SKPASS_HID_FRAME_BYTES];
    size_t size;
    uint8_t sequence, count, next;
} skpass_hid_assembly_t;
size_t skpass_hid_fragments(size_t frame_size);
int skpass_hid_fragment(const uint8_t *frame, size_t size, uint8_t sequence,
                        uint8_t index, uint8_t report[64]);
/* 1: complete frame, 0: more fragments needed, -1: invalid/lost sequence. */
int skpass_hid_receive(skpass_hid_assembly_t *a, const uint8_t *report, size_t size);
#ifdef __cplusplus
}
#endif
#endif
