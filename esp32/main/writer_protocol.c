#include "writer_protocol.h"
#include <string.h>
bool writer_decode_request(const uint8_t *p, size_t n, writer_request_t *r)
{
    if (!p || !r || n != WRITER_REQUEST_BYTES) return false;
    memset(r, 0, sizeof(*r));
    r->id = (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    r->uid_len = p[4];
    if (!r->id || (r->uid_len != 4 && r->uid_len != 7 && r->uid_len != 10)) return false;
    memcpy(r->uid, p+5, 10); memcpy(r->key, p+15, 6); memcpy(r->bcd, p+21, 10);
    r->options=p[31];
    if (r->options & ~0x01) return false;
    for (size_t i=r->uid_len; i<10; ++i) if (r->uid[i]) return false;
    bool nonzero = false;
    for (size_t i=0; i<10; ++i) {
        if ((r->bcd[i]>>4)>9 || (r->bcd[i]&15)>9) return false;
        nonzero |= r->bcd[i] != 0;
    }
    return nonzero;
}
size_t writer_encode_result(const writer_result_t *r, uint8_t out[WRITER_RESULT_BYTES])
{
    if (!r || !out) return 0;
    for (unsigned i=0; i<4; ++i) out[i]=(uint8_t)(r->id>>(i*8));
    out[4]=r->status; out[5]=r->stage; out[6]=r->detail; out[7]=r->uid_len;
    memcpy(out+8,r->uid,10); out[18]=r->verified_mask; memcpy(out+19,r->blocks,32);
    return WRITER_RESULT_BYTES;
}
void writer_build_blocks(const writer_request_t *r, uint8_t out[32])
{
    memset(out,0,32); memcpy(out+22,r->bcd,10); /* block 2 offset 6 */
}
