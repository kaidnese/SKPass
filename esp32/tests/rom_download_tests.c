#include "rom_stubs/rom_platform.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../main/rom_download.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static int disabled,allocation=1,clock_ms,disconnected,powered_down,forced,resets;
static void (*task)(void *);
static jmp_buf finished;
bool esp_efuse_read_field_bit(int field) {return field==disabled;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)name;(void)stack;(void)arg;(void)priority;(void)handle;
    if(!allocation)return 0;task=fn;return pdPASS;
}
void vTaskDelay(TickType_t ticks) {clock_ms+=ticks;}
void gpio_set_level(int pin,int level) {CHECK(pin==6 && level==0 && clock_ms>=600);powered_down=1;}
void tud_disconnect(void) {CHECK(powered_down);disconnected=1;}
void mock_set_bit(uint32_t reg,uint32_t bit) {CHECK(reg==0x6000812C && bit==1 && disconnected && clock_ms>=700);forced=1;}
void esp_rom_software_reset_system(void) {CHECK(forced);++resets;longjmp(finished,1);}
int main(void) {
    disabled=1;CHECK(Rom_Download_Enter()==ESP_ERR_NOT_SUPPORTED);CHECK(!s_requested && !task);
    disabled=2;CHECK(Rom_Download_Enter()==ESP_ERR_NOT_SUPPORTED);CHECK(!s_requested && !task);
    disabled=0;allocation=0;CHECK(Rom_Download_Enter()==ESP_ERR_NO_MEM);CHECK(!s_requested && !task);
    allocation=1;CHECK(Rom_Download_Enter()==ESP_OK);CHECK(Rom_Download_Is_Requested());
    CHECK(Rom_Download_Enter()==ESP_ERR_INVALID_STATE);CHECK(!resets);
    if(!setjmp(finished))task(NULL);
    CHECK(resets==1 && powered_down && disconnected && forced && clock_ms==700);
    puts("ROM entry: eFuse checks, allocation failure, ACK grace, VEN, disconnect and system reset passed");
    return 0;
}
