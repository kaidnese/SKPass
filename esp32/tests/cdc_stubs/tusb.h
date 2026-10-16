#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CFG_TUD_CDC 1
bool tud_mounted(void);
bool tud_cdc_connected(void);
unsigned tud_cdc_available(void);
unsigned tud_cdc_read(void *,unsigned);
unsigned tud_cdc_write(const void *,unsigned);
void tud_cdc_write_flush(void);
void tud_cdc_write_clear(void);
bool tud_hid_n_ready(unsigned);
bool tud_hid_n_report(unsigned,unsigned,const void *,unsigned);
