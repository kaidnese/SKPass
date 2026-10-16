#include "aime_reader.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    NORMAL, FACTORY, AUTH_TIMEOUT, AUTH_FAILURE, NACK_ONCE, READ_TIMEOUT,
    SHORT_TWICE, READ_FAILURE, RESELECT_FAILURE, UID_CHANGED,
    RETRY_AUTH_FAILURE, OPEN_CARD, TIMEOUT_RESELECT_FAILURE, RF_STATUS
};
static int mode;
static unsigned selections, auths, configured_auths, factory_auths;
static unsigned reads[3], retried, recovered, delays;
static TickType_t clock_ticks, last_exchange;
static bool authenticated, needs_selection;
static const uint8_t configured[6] = {0x57,0x43,0x43,0x46,0x76,0x32};
static pn7150_card_info_t card = {
    .discovery_id=3, .rf_interface=0x80, .protocol=0x80,
    .uid={0x0B,0x58,0x88,0x4F}, .uid_length=4, .sak=8, .sak_valid=true
};
static void check(bool condition, const char *name)
{
    if (!condition) {fprintf(stderr, "FAILED mode=%d: %s\n", mode, name); exit(1);}
}
void vTaskDelay(TickType_t ticks)
{
    /* Even with a partially elapsed tick there must be >=5ms of waiting. */
    check(ticks > 0 && (ticks-1)*portTICK_PERIOD_MS >= 5, "nonzero minimum frame gap");
    clock_ticks += ticks; ++delays;
}
TickType_t xTaskGetTickCount(void) {return clock_ticks;}
const char *esp_err_to_name(esp_err_t err)
{
    return err == ESP_OK ? "ESP_OK" : err == ESP_ERR_TIMEOUT ? "ESP_ERR_TIMEOUT" : "ESP_ERR_INVALID_STATE";
}
void SKPass_USB_Debug(const char *text)
{
    if (strstr(text,"MIFARE RETRY block=")) ++retried;
    if (strstr(text,"MIFARE RETRY recovered")) ++recovered;
}
esp_err_t Pn7150_ReActivate(i2c_master_bus_handle_t bus, const pn7150_card_info_t *selected)
{
    (void)bus;
    check(selected->discovery_id==3 && selected->uid_length==4 &&
          !memcmp(selected->uid,card.uid,4), "reselect original discovery ID and exact UID");
    ++selections; authenticated=false; needs_selection=false;
    if (mode==RESELECT_FAILURE || mode==UID_CHANGED ||
        (mode==TIMEOUT_RESELECT_FAILURE && selections==2)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t bus,
    const uint8_t *tx, size_t len, uint8_t *rx, size_t capacity, size_t *size)
{
    (void)bus;
    check(capacity>=18, "response capacity");
    check(!needs_selection, "no blind DATA retransmit after error/timeout");
    check((clock_ticks-last_exchange)*portTICK_PERIOD_MS>=5, "gap before each RF command");
    last_exchange=clock_ticks;
    *size=0;
    if (tx[0]==0x40) {
        check(len==9 && tx[1]==0 && tx[2]==0x10 && selections, "sector0 embedded KeyA auth");
        ++auths;
        bool factory=tx[3]==0xFF;
        if(factory) ++factory_auths; else {
            ++configured_auths;
            check(!memcmp(tx+3,configured,6), "configured key preserved on retries");
        }
        if(mode==AUTH_TIMEOUT && auths==1) {needs_selection=true;return ESP_ERR_TIMEOUT;}
        rx[0]=0x40; rx[1]=(mode==AUTH_FAILURE || (mode==FACTORY && !factory) ||
                          (mode==RETRY_AUTH_FAILURE && auths==2)) ? 3 : 0;
        *size=2; authenticated=rx[1]==0;
        if(!authenticated) needs_selection=true;
        return ESP_OK;
    }
    check(len==3 && tx[0]==0x10 && tx[1]==0x30 && tx[2]<3, "read-only blocks0..2");
    unsigned block=tx[2];
    if (!authenticated && mode!=OPEN_CARD) {
        check(block==0 && !selections, "initial unauthenticated probe only once");
        rx[0]=0x10;rx[1]=4;rx[2]=0;*size=3;needs_selection=true;return ESP_OK;
    }
    ++reads[block];
    bool fail=block==1 && ((mode==NACK_ONCE || mode==READ_TIMEOUT ||
        mode==RETRY_AUTH_FAILURE || mode==TIMEOUT_RESELECT_FAILURE || mode==RF_STATUS) ? reads[block]==1 :
        mode==SHORT_TWICE ? reads[block]<=2 : mode==READ_FAILURE);
    if (fail) {
        needs_selection=true; authenticated=false;
        if(mode==READ_TIMEOUT || mode==TIMEOUT_RESELECT_FAILURE) {
            /* Partial old response must not be copied to the next block. */
            rx[0]=0x10;memset(rx+1,0xEE,8);*size=9;return ESP_ERR_TIMEOUT;
        }
        if(mode==SHORT_TWICE) {rx[0]=0x10;rx[1]=0;*size=2;return ESP_OK;}
        if(mode==RF_STATUS) {rx[0]=0x10;memset(rx+1,0xEE,16);rx[17]=0xB0;*size=18;return ESP_OK;}
        rx[0]=0x10;rx[1]=0xB0;*size=2;return ESP_OK;
    }
    rx[0]=0x10;memset(rx+1,0x30+block,16);rx[17]=0;*size=18;return ESP_OK;
}
static aime_card_event_t run(int scenario)
{
    mode=scenario; selections=auths=configured_auths=factory_auths=0;
    memset(reads,0,sizeof(reads));retried=recovered=delays=0;
    clock_ticks=last_exchange=0;authenticated=needs_selection=false;
    aime_card_event_t event;
    Aime_Read_Card((void *)1,&card,&event);
    check(event.card_type==0x10 && !memcmp(event.idm,card.uid,4), "Classic event retains UID");
    if (!event.status) {
        check(event.mifare_len==48, "success requires all 48 bytes");
        for(unsigned b=0;b<3;++b) for(unsigned i=0;i<16;++i)
            check(event.mifare[16*b+i]==0x30+b, "correct block data, no stale/partial bytes");
    } else {
        check(event.mifare_len==0, "failure never publishes partial sector");
        for(unsigned i=0;i<48;++i) check(event.mifare[i]==0, "partial sector cleared");
    }
    return event;
}
int main(void)
{
    check(run(NORMAL).status==0 && auths==1 && selections==1, "normal read");
    check(run(OPEN_CARD).status==0 && !auths && !selections, "unauthenticated card compatibility");
    check(run(FACTORY).status==0 && configured_auths==3 && factory_auths==1, "factory key fallback");
    check(run(AUTH_TIMEOUT).status==0 && auths==2 && selections==2, "authentication timeout soft recovery");
    check(run(AUTH_FAILURE).status==0x12 && auths==6 && !reads[0], "both keys exhaust bounded authentication retries");
    check(run(NACK_ONCE).status==0 && reads[1]==2 && recovered==1 && reads[0]==1, "retry only failed block after reauth");
    check(run(READ_TIMEOUT).status==0 && reads[1]==2 && selections==2, "timeout requires fresh RF selection");
    check(run(SHORT_TWICE).status==0 && reads[1]==3 && recovered==1, "second extra retry recovers malformed frames");
    check(run(RF_STATUS).status==0 && reads[1]==2, "nonzero RF status never accepted as data");
    check(run(READ_FAILURE).status==0x13 && reads[1]==3 && !reads[2] && !factory_auths, "bounded read failure, no unrelated key fallback");
    check(run(RESELECT_FAILURE).status==0x13 && !auths, "lost RF session aborts retries");
    check(run(UID_CHANGED).status==0x13 && !auths, "changed card during reselect aborts");
    check(run(TIMEOUT_RESELECT_FAILURE).status==0x13 && reads[1]==1 && auths==1, "failed timeout synchronization stops DATA");
    check(run(RETRY_AUTH_FAILURE).status==0 && auths==3 && reads[1]==2 && retried==2, "retry auth failure stays within block budget");
    puts("MIFARE reader mock tests passed: 14 scenarios; spacing, bounded retries, reauth, partial-data guards.");
    return 0;
}
