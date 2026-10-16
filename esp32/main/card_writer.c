#include "card_writer.h"
#include <string.h>
static bool exchange(i2c_master_bus_handle_t bus, const uint8_t *cmd, size_t n,
                     uint8_t *rx, size_t *size, writer_result_t *r)
{
    *size=0;
    esp_err_t ret=Pn7150_Tag_Transceive(bus,cmd,n,rx,255,size);
    r->detail=ret==ESP_OK ? (*size ? rx[*size-1] : 0xFF) : 0xFF;
    return ret==ESP_OK;
}
static bool read_block(i2c_master_bus_handle_t bus, uint8_t b, uint8_t out[16], writer_result_t *r)
{
    uint8_t cmd[]={0x10,0x30,b},rx[255]; size_t n;
    if (!exchange(bus,cmd,sizeof(cmd),rx,&n,r)) return false;
    if (n!=18 || rx[0]!=0x10 || rx[17]!=0) {
        if (n==3 && rx[0]==0x10 && rx[2]==0) r->detail=rx[1];
        return false;
    }
    memcpy(out,rx+1,16); return true;
}
static bool write_ack(i2c_master_bus_handle_t bus, const uint8_t *cmd, size_t size, writer_result_t *r)
{
    uint8_t rx[255]; size_t n;
    if (!exchange(bus,cmd,size,rx,&n,r)) return false;
    /* PN7150 example uses trailing status 00. If a MIFARE RF ACK is
     * present, require 0A; a NACK such as 04 is not write success. */
    if (n==2 && rx[0]==0x10 && rx[1]==0) return true;
    if (n==3 && rx[0]==0x10 && rx[1]==0x0A && rx[2]==0) return true;
    if (n==3 && rx[0]==0x10 && rx[2]==0) r->detail=rx[1];
    return false;
}
void CardWriter_Write(i2c_master_bus_handle_t bus, const pn7150_card_info_t *c,
                      const writer_request_t *q, writer_result_t *r)
{
    memset(r,0,sizeof(*r)); r->id=q->id; r->uid_len=q->uid_len;
    memcpy(r->uid,q->uid,10); r->status=WRITE_REJECTED;
    /* Restrict to confirmed Classic 1K, and the exact selected physical UID. */
    if ((q->uid_len!=4 && q->uid_len!=7 && q->uid_len!=10) ||
        c->mode_technology!=0 || c->protocol!=0x80 || c->rf_interface!=0x80 ||
        !c->sak_valid || c->sak!=0x08 || c->uid_length!=q->uid_len ||
        memcmp(c->uid,q->uid,q->uid_len)) return;
    uint8_t auth[9]={0x40,0,0x10},rx[255],before[32],desired[32]; size_t n;
    memcpy(auth+3,q->key,6); r->stage=1; r->status=WRITE_AUTH_FAILED;
    if (!exchange(bus,auth,sizeof(auth),rx,&n,r) || n!=2 || rx[0]!=0x40 || rx[1]) return;
    r->stage=2; r->status=WRITE_PREFLIGHT_FAILED;
    if (!read_block(bus,1,before,r) || !read_block(bus,2,before+16,r)) return;
    writer_build_blocks(q,desired);
    for (uint8_t b=1; b<=2; ++b) {
        r->stage=(uint8_t)(3+(b-1)*2); r->status=WRITE_FAILED;
        uint8_t start[]={0x10,0xA0,b},data[17]={0x10};
        /* Only blocks 1 and 2 can reach this path. Never block 0/trailer 3. */
        memcpy(data+1,desired+(b-1)*16,16);
        if (!write_ack(bus,start,sizeof(start),r)) return;
        ++r->stage;
        if (!write_ack(bus,data,sizeof(data),r)) return;
    }
    if (q->options & 0x01) {
        static const uint8_t sega_key[6]={0x57,0x43,0x43,0x46,0x76,0x32};
        static const uint8_t trailer_tail[10]={0xFF,0x07,0x80,0x69,
                                               0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        uint8_t start[]={0x10,0xA0,0x03},data[17]={0x10};
        memcpy(data+1,sega_key,6); memcpy(data+7,trailer_tail,10);
        r->stage=8; r->status=WRITE_FAILED;
        if (!write_ack(bus,start,sizeof(start),r) || !write_ack(bus,data,sizeof(data),r)) return;
    }
    r->status=WRITE_VERIFY_FAILED;
    for (uint8_t b=1; b<=2; ++b) {
        r->stage=(uint8_t)(7+b-1);
        if (!read_block(bus,b,r->blocks+(b-1)*16,r)) return;
        r->verified_mask|=(uint8_t)(1u<<(b-1));
        if (memcmp(r->blocks+(b-1)*16,desired+(b-1)*16,16)) return;
    }
    if (q->options & 0x01) {
        uint8_t trailer[16]; static const uint8_t sega_key[6]={0x57,0x43,0x43,0x46,0x76,0x32};
        uint8_t verify_auth[]={0x40,0x00,0x10},verify_rx[255]; size_t verify_n=0;
        memcpy(verify_auth+3,sega_key,6);
        if (!exchange(bus,verify_auth,sizeof(verify_auth),verify_rx,&verify_n,r) ||
            verify_n!=2 || verify_rx[0]!=0x40 || verify_rx[1]) return;
        r->stage=9;
        if (!read_block(bus,3,trailer,r) || memcmp(trailer,sega_key,6)) return;
        r->verified_mask|=4;
    }
    r->status=WRITE_SUCCESS; r->stage=0; r->detail=0;
}
