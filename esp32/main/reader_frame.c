#include "reader_frame.h"
#include <string.h>
uint16_t reader_crc16(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
size_t reader_encode_frame(uint8_t msg, const uint8_t *payload, size_t size,
                           uint8_t *out, size_t capacity)
{
    if (!out || size > READER_MAX_PAYLOAD || capacity < size + 8 || (size && !payload)) return 0;
    out[0] = 0xCA; out[1] = 0x5A; out[2] = 1; out[3] = msg;
    out[4] = (uint8_t)size; out[5] = (uint8_t)(size >> 8);
    if (size) memcpy(out + 6, payload, size);
    uint16_t crc = reader_crc16(out + 2, size + 4);
    out[size + 6] = (uint8_t)crc; out[size + 7] = (uint8_t)(crc >> 8);
    return size + 8;
}
void reader_decoder_feed(reader_decoder_t *d, const uint8_t *p, size_t n,
                         reader_frame_callback_t cb, void *context)
{
    if (!d || !p || !cb) return;
    for (size_t i=0; i<n; ++i) {
        if (d->size==sizeof(d->data)) {memmove(d->data,d->data+1,--d->size);}
        d->data[d->size++]=p[i];
        while (d->size) {
            if (d->data[0]!=0xCA || (d->size>=2 && d->data[1]!=0x5A)) {
                memmove(d->data,d->data+1,--d->size); continue;
            }
            if (d->size<6) break;
            size_t len=d->data[4]|((size_t)d->data[5]<<8);
            if (d->data[2]!=1 || len>READER_MAX_PAYLOAD) {
                memmove(d->data,d->data+1,--d->size); continue;
            }
            if (d->size<len+8) break;
            uint16_t crc=d->data[len+6]|((uint16_t)d->data[len+7]<<8);
            if (reader_crc16(d->data+2,len+4)!=crc) {
                memmove(d->data,d->data+1,--d->size); continue;
            }
            cb(d->data[3],d->data+6,len,context);
            d->size-=len+8;
            memmove(d->data,d->data+len+8,d->size);
        }
    }
}
