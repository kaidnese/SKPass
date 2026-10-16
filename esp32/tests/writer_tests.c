#include "writer_platform.h"
#include "card_writer.h"
#include "reader_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned exchanges,writes,pending_block;
static int mode;
static uint8_t memory[4][16];
static void check(bool ok,const char *name) {if(!ok){fprintf(stderr,"FAILED: %s\n",name);exit(1);}}
static void reset(int m) {mode=m;exchanges=writes=pending_block=0;memset(memory,0x55,sizeof(memory));}
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t bus,const uint8_t *tx,size_t n,uint8_t *rx,size_t cap,size_t *size)
{
    (void)bus;check(cap>=18,"rx capacity");++exchanges;
    if(tx[0]==0x40){check(n==9 && tx[1]==0 && tx[2]==0x10,"embedded sector0 keyA");rx[0]=0x40;rx[1]=mode==1?3:0;*size=2;return ESP_OK;}
    check(tx[0]==0x10,"tag command prefix");
    if(n==3 && tx[1]==0x30){
        check(tx[2]==1 || tx[2]==2,"read only requested data blocks");
        if(mode==2 && exchanges==2){rx[0]=0x10;rx[1]=4;rx[2]=0;*size=3;return ESP_OK;}
        rx[0]=0x10;memcpy(rx+1,memory[tx[2]],16);rx[17]=0;*size=18;
        if(mode==4 && writes==2)rx[1]^=1;
        return ESP_OK;
    }
    if(n==3 && tx[1]==0xA0){
        check(tx[2]==1 || tx[2]==2,"NEVER write block0 or trailer3");
        if(mode==3){rx[0]=0x10;rx[1]=4;rx[2]=0;*size=3;return ESP_OK;}
        pending_block=tx[2];rx[0]=0x10;rx[1]=0x0A;rx[2]=0;*size=3;return ESP_OK;
    }
    check(n==17 && (pending_block==1 || pending_block==2),"two part write data");
    memcpy(memory[pending_block],tx+1,16);pending_block=0;++writes;
    rx[0]=0x10;rx[1]=0;*size=2;return ESP_OK;
}
static unsigned received;
static void incoming(uint8_t msg,const uint8_t *p,size_t n,void *context)
{
    (void)context;writer_request_t q;
    check(msg==0x20 && writer_decode_request(p,n,&q),"valid framed write callback");++received;
}
int main(void)
{
    uint8_t p[32]={0x78,0x56,0x34,0x12,4,0x11,0x22,0x33,0x44};
    memset(p+15,0xFF,6);const uint8_t bcd[]={0x01,0x03,0x62,0x61,0x38,0x71,0x95,0x96,0x14,0x52};memcpy(p+21,bcd,10);
    writer_request_t q;check(writer_decode_request(p,sizeof(p),&q),"request valid");
    check(q.id==0x12345678,"LE request id");uint8_t saved=p[21];p[21]=0xFA;
    check(!writer_decode_request(p,sizeof(p),&q),"BCD invalid rejected");p[21]=saved;
    check(writer_decode_request(p,sizeof(p),&q),"restore request");
    p[4]=3;check(!writer_decode_request(p,sizeof(p),&q),"invalid UID length rejected");p[4]=4;
    p[9]=1;check(!writer_decode_request(p,sizeof(p),&q),"nonzero UID padding rejected");p[9]=0;
    check(!writer_decode_request(p,sizeof(p)-1,&q),"short write request rejected");
    check(writer_decode_request(p,sizeof(p),&q),"valid request after invalid cases");
    pn7150_card_info_t c={.rf_interface=0x80,.protocol=0x80,.uid_length=4,.sak=8,.sak_valid=true};memcpy(c.uid,q.uid,4);
    writer_result_t r;uint8_t expected[32];writer_build_blocks(&q,expected);
    reset(0);CardWriter_Write((void *)1,&c,&q,&r);
    check(r.status==WRITE_SUCCESS && r.verified_mask==3 && writes==2 && exchanges==9,"write and both readbacks");
    check(!memcmp(memory[1],expected,32),"BCD offset6 and block1 zero");
    for(int i=0;i<16;++i)check(memory[0][i]==0x55 && memory[3][i]==0x55,"UID/trailer unchanged");
    reset(1);CardWriter_Write((void *)1,&c,&q,&r);check(r.status==WRITE_AUTH_FAILED && !writes && exchanges==1,"auth failure no writes");
    reset(2);CardWriter_Write((void *)1,&c,&q,&r);check(r.status==WRITE_PREFLIGHT_FAILED && !writes,"preflight failure no writes");
    reset(3);CardWriter_Write((void *)1,&c,&q,&r);check(r.status==WRITE_FAILED && r.detail==4 && !writes && exchanges==4,"NACK stops before data write");
    reset(4);CardWriter_Write((void *)1,&c,&q,&r);check(r.status==WRITE_VERIFY_FAILED && r.verified_mask==1,"readback mismatch rejected");
    reset(0);c.uid[0]^=1;CardWriter_Write((void *)1,&c,&q,&r);check(r.status==WRITE_REJECTED && !exchanges,"wrong UID no NFC commands");c.uid[0]^=1;
    c.sak=0x18;CardWriter_Write((void *)1,&c,&q,&r);check(r.status==WRITE_REJECTED && !exchanges,"not Classic1K rejected");
    uint8_t frame[520],bad[520];size_t n=reader_encode_frame(0x20,p,sizeof(p),frame,sizeof(frame));memcpy(bad,frame,n);bad[7]^=1;
    reader_decoder_t decoder={0};reader_decoder_feed(&decoder,frame,3,incoming,NULL);check(!received,"split request waits");
    reader_decoder_feed(&decoder,frame+3,n-3,incoming,NULL);check(received==1,"split request parsed");
    reader_decoder_feed(&decoder,bad,n,incoming,NULL);check(received==1,"bad CRC never dispatches write");
    reader_decoder_feed(&decoder,(const uint8_t *)"noise",5,incoming,NULL);
    reader_decoder_feed(&decoder,frame,n,incoming,NULL);check(received==2,"noise recovery");
    puts("Writer mock tests passed: UID binding, blocks1/2 only, failure guards, CRC parser");return 0;
}
