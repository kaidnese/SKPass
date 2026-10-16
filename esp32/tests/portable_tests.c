#include "reader_frame.h"
#include "SKPass.h"
#include "firmware_identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned sent;
static uint8_t latest[9];
static void check(int ok, const char *name) {
    if (!ok) {fprintf(stderr,"FAILED: %s\n",name);exit(1);}
}
static int sender(const uint8_t *report, void *context) {
    (void)context;memcpy(latest,report,9);++sent;return 9;
}
int main(void) {
    const uint8_t digest[32]={0x5d,0x3a,0x06,0x32,0x98,0xa0,0x2c,0xfe,
        0x9b,0x02,0x89,0xc1,0x0f,0x7d,0x04,0xe6,0xaa,0x16,0xf9,0x58,
        0x26,0x55,0x97,0x60,0x14,0x89,0x42,0x87,0x8f,0x19,0xa4,0x74};
    char hash[65];SKPass_Format_Firmware_Hash(digest,hash);
    check(!strcmp(hash,"5d3a063298a02cfe9b0289c10f7d04e6aa16f95826559760148942878f19a474"),
          "complete ELF fingerprint, independent of IDF's nine-character prefix setting");
    uint8_t raw[520], payload[512] = {0};
    check(reader_crc16((const uint8_t *)"123456789",9)==0x29B1,"CRC vector");
    size_t n=reader_encode_frame(0x10,payload,512,raw,sizeof(raw));
    check(n==520 && raw[4]==0 && raw[5]==2,"max frame LE length");
    uint16_t crc=reader_crc16(raw+2,516);
    check(raw[518]==(uint8_t)crc && raw[519]==(uint8_t)(crc>>8),"CRC coverage");
    check(reader_encode_frame(0x11,NULL,0,raw,8)==8,"empty removal");
    check(!reader_encode_frame(0x10,NULL,1,raw,520),"null payload");
    check(!reader_encode_frame(0x10,payload,513,raw,520),"overlength");
    check(!reader_encode_frame(0x10,payload,512,raw,519),"capacity");
    SKPass c;SKPass_Init(&c,sender,NULL);
    uint8_t id[8]={0x82,1,2,3,4,5,6,7};
    check(SKPass_Send(&c,2,id,10)==9 && latest[0]==2 && !memcmp(latest+1,id,8),"HID 9 bytes");
    check(SKPass_Send(&c,2,id,3009)==0 && sent==1,"global suppression");
    check(SKPass_Send(&c,2,id,3010)==9 && sent==2,"same card repeat");
    SKPass_FromISO14443(&c,id,4,6010);
    check(latest[1]==2 && latest[5]==0 && sent==3,"original UID mapping");
    check(SKPass_HID_ReportDescriptorSize==42 && SKPass_HID_ReportDescriptor[1]==0xCA &&
          SKPass_HID_ReportDescriptor[2]==0xFF,"HID identity descriptor");
    puts("Portable CRC/framing and original SKPass HID tests passed");return 0;
}
