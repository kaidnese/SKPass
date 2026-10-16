#include "smartcard_probe.h"
#include "freertos/task.h"
#include <string.h>
TickType_t xTaskGetTickCount(void){return 0;}
void vTaskDelay(TickType_t ticks){(void)ticks;}
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t bus,const uint8_t *tx,size_t size,
                              uint8_t *rx,size_t capacity,size_t *length) {
    (void)bus;(void)tx;(void)size;(void)rx;(void)capacity;*length=0;return ESP_ERR_TIMEOUT;
}
/* Actual firmware JSON encoder used by the desktop transport round trip. */
size_t nfc_test_part(unsigned scan,unsigned part,const char *kind,char *out,size_t capacity) {
    smartcard_info_t i={0};i.scan=scan;i.protocol=3;i.rf_interface=1;i.recognized=true;
    strcpy(i.uid,"011E65740A055390");strcpy(i.kind,kind);strcpy(i.currency,"JPY");
    i.has_balance=true;i.balance=1234;i.records=2;
    for(unsigned x=0;x<2;x++){strcpy(i.history[x].date,"20261013");i.history[x].type=1;i.history[x].entry=0x1234;i.history[x].exit=0x5678;}
    return SmartCard_Encode_Part(&i,part,out,capacity);
}
