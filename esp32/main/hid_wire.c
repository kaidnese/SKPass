#include "hid_wire.h"
#include <string.h>
size_t skpass_hid_fragments(size_t size) {
    return size && size <= SKPASS_HID_FRAME_BYTES
        ? (size + SKPASS_HID_CHUNK_BYTES - 1) / SKPASS_HID_CHUNK_BYTES : 0;
}
int skpass_hid_fragment(const uint8_t *frame, size_t size, uint8_t sequence,
                        uint8_t index, uint8_t report[64]) {
    size_t count = skpass_hid_fragments(size), offset = (size_t)index * SKPASS_HID_CHUNK_BYTES;
    if (!frame || !report || !count || index >= count) return 0;
    size_t n = size - offset;
    if (n > SKPASS_HID_CHUNK_BYTES) n = SKPASS_HID_CHUNK_BYTES;
    memset(report, 0, 64);
    report[0] = SKPASS_HID_REPORT_ID; report[1] = 1;
    report[2] = sequence; report[3] = index; report[4] = (uint8_t)count; report[5] = (uint8_t)n;
    memcpy(report + 6, frame + offset, n);
    return 1;
}
int skpass_hid_receive(skpass_hid_assembly_t *a, const uint8_t *p, size_t size) {
    if (!a) return -1;
    if (!p || size != 64 || p[0] != SKPASS_HID_REPORT_ID || p[1] != 1 ||
        !p[4] || p[4] > 9 || p[3] >= p[4] || !p[5] || p[5] > SKPASS_HID_CHUNK_BYTES ||
        (p[3] + 1 < p[4] && p[5] != SKPASS_HID_CHUNK_BYTES)) goto bad;
    if (p[3] == 0) { a->size = 0; a->sequence = p[2]; a->count = p[4]; a->next = 0; }
    if (p[2] != a->sequence || p[4] != a->count || p[3] != a->next ||
        a->size + p[5] > sizeof(a->data)) goto bad;
    memcpy(a->data + a->size, p + 6, p[5]); a->size += p[5]; ++a->next;
    return a->next == a->count ? 1 : 0;
bad:
    a->size = 0; a->count = 0; a->next = 0;
    return -1;
}
