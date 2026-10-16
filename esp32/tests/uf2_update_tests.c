#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Exercise the real MSC/OTA implementation, with mocked flash and RTOS.
 * Including it also allows each independent scenario to reset its globals. */
#include "../main/uf2_update.c"

#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static esp_partition_t target={APP_SIZE},running={APP_SIZE};
static uint8_t flash[512];
static void (*scheduled_task)(void *);
static uint32_t notifications,clock_ms;
static int boots,restarts,disconnects,task_deleted;
static esp_err_t end_result,description_result,boot_result;
static bool task_allocation,notification_pending,status_during_wait;
static const char *project;
static jmp_buf restart_return;

int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)name;(void)stack;(void)arg;(void)priority;
    if(!task_allocation)return 0;
    scheduled_task=fn;if(handle)*handle=&scheduled_task;return pdPASS;
}
void vTaskDelete(TaskHandle_t handle) {CHECK(handle==&scheduled_task);scheduled_task=NULL;++task_deleted;}
void vTaskDelay(TickType_t ticks) {clock_ms+=ticks;}
int xTaskNotify(TaskHandle_t handle,uint32_t bits,eNotifyAction action) {
    CHECK(handle==&scheduled_task && action==eSetBits && s_complete);
    notifications|=bits;notification_pending=true;return pdPASS;
}
int xTaskNotifyWait(uint32_t clear_entry,uint32_t clear_exit,uint32_t *value,TickType_t wait) {
    notifications&=~clear_entry;
    if(!notification_pending) {
        CHECK(wait!=portMAX_DELAY);
        if(status_during_wait) {
            char bytes[512];clock_ms+=500;status_during_wait=false;
            CHECK(tud_msc_read10_cb(0,DATA_START+1,0,bytes,512)==512);
        } else clock_ms+=wait;
    }
    int received=notification_pending?pdPASS:0;
    *value=notifications;notifications&=~clear_exit;
    notification_pending=false;
    return received;
}
int esp_reset_reason(void) {return ESP_RST_SW;}
void esp_restart(void) {++restarts;longjmp(restart_return,1);}
esp_err_t esp_flash_get_size(void *chip,uint32_t *size) {(void)chip;*size=16u*1024u*1024u;return ESP_OK;}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *part) {(void)part;return &target;}
const esp_partition_t *esp_ota_get_running_partition(void) {return &running;}
esp_err_t esp_ota_begin(const esp_partition_t *part,size_t size,esp_ota_handle_t *handle) {CHECK(part==&target && size==512);*handle=1;return ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) {CHECK(handle==1);return ESP_OK;}
esp_err_t esp_ota_write_with_offset(esp_ota_handle_t handle,const void *bytes,size_t size,uint32_t offset) {
    CHECK(handle==1 && offset+size<=sizeof(flash));memcpy(flash+offset,bytes,size);return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {CHECK(handle==1);return end_result;}
esp_err_t esp_ota_get_partition_description(const esp_partition_t *part,esp_app_desc_t *desc) {
    CHECK(part==&target);memset(desc,0,sizeof(*desc));snprintf(desc->project_name,sizeof(desc->project_name),"%s",project);return description_result;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *part) {
    CHECK(part==&target && scheduled_task && !s_complete);if(boot_result==ESP_OK)++boots;return boot_result;
}
esp_err_t esp_partition_read(const esp_partition_t *part,uint32_t offset,void *data,size_t size) {
    CHECK(part==&target && offset+size<=sizeof(flash));memcpy(data,flash+offset,size);return ESP_OK;
}
bool tud_disconnect(void) {CHECK(boots==1 && s_complete);++disconnects;return true;}
void tud_msc_set_sense(uint8_t lun,uint8_t sense,uint8_t asc,uint8_t ascq) {(void)lun;(void)sense;(void)asc;(void)ascq;}

static void reset(void) {
    s_boot_magic=UPDATE_MAGIC;s_active=s_requested=s_complete=false;s_restart_task=NULL;
    s_target=NULL;s_ota=0;s_blocks=s_received=0;memset(s_seen,0,sizeof(s_seen));memset(s_failure,0,sizeof(s_failure));
    memset(flash,0,sizeof(flash));scheduled_task=NULL;notifications=clock_ms=0;
    notification_pending=status_during_wait=false;
    boots=restarts=disconnects=task_deleted=0;end_result=description_result=boot_result=ESP_OK;
    task_allocation=true;project="SKPass";
    CHECK(Uf2_Update_Boot() && Uf2_Update_Is_Active() && s_boot_magic==0 && !Uf2_Update_Has_Error());
}
static void block(uint32_t number,uint8_t bytes[512]) {
    memset(bytes,0,512);put32(bytes,0x0A324655u);put32(bytes+4,0x9E5D5157u);put32(bytes+8,0x2000u);
    put32(bytes+12,number*256);put32(bytes+16,256);put32(bytes+20,number);put32(bytes+24,2);
    put32(bytes+28,FAMILY_ID);memset(bytes+32,(int)(number+1),256);put32(bytes+508,0x0AB16F30u);
}
static void upload(void) {
    uint8_t b[512];block(0,b);CHECK(tud_msc_write10_cb(0,DATA_START+2,0,b,512)==512);
    CHECK(boots==0 && !scheduled_task && !s_complete);
    block(1,b);tud_msc_write10_cb(0,DATA_START+3,0,b,512);
}
static void status(char out[513]) {
    memset(out,0,513);CHECK(tud_msc_read10_cb(0,DATA_START+1,0,out,512)==512);
}
static void run_restart(void) {
    CHECK(scheduled_task && boots==1 && s_complete);
    if(setjmp(restart_return)==0)scheduled_task(NULL);
    CHECK(restarts==1 && disconnects==1 && s_boot_magic==0);
    CHECK(!Uf2_Update_Boot()); /* Next software boot returns to normal reader. */
}
static void rejected(void) {
    char text[513];status(text);CHECK(strncmp(text,"ERROR:",6)==0);
    CHECK(boots==0 && restarts==0 && !scheduled_task && !s_complete);
}
int main(void) {
    char text[513];uint8_t b[512];
    reset();block(0,b);tud_msc_write10_cb(0,DATA_START+2,0,b,512);status(text);
    CHECK(strncmp(text,"READY",5)==0 && boots==0 && !scheduled_task);
    reset();end_result=ESP_FAIL;upload();rejected();
    reset();project="unrelated_app";upload();rejected();
    reset();description_result=ESP_FAIL;upload();rejected();
    reset();task_allocation=false;upload();rejected();
    reset();boot_result=ESP_FAIL;upload();rejected();CHECK(task_deleted==1);
    reset();block(0,b);tud_msc_write10_cb(0,DATA_START+2,0,b,512);b[32]^=1;
    CHECK(tud_msc_write10_cb(0,DATA_START+2,0,b,512)==-1);rejected();
    reset();upload();CHECK(s_complete && boots==1 && restarts==0);
    // Post-commit duplicate/conflicting/mixed writes cannot revoke success.
    block(0,b);b[32]^=1;CHECK(tud_msc_write10_cb(0,DATA_START+2,0,b,512)==512);
    CHECK(!Uf2_Update_Has_Error());status(text);CHECK(strncmp(text,"COMPLETE",8)==0);
    run_restart();CHECK(clock_ms==2100);
    reset();project="pn7150_cardio";upload();status(text);run_restart();CHECK(clock_ms==2100);
    reset();upload();run_restart();CHECK(clock_ms==17100); /* Explorer/no STATUS reader. */
    reset();upload();status_during_wait=true;run_restart();CHECK(clock_ms==2600);
    reset();upload();CHECK(tud_msc_read10_cb(0,DATA_START+1,1,text,7)==7);
    run_restart();CHECK(clock_ms==17100); /* Partial status is not an acknowledgement. */
    puts("UF2: validation failures preserve boot, COMPLETE acknowledgement/grace, fallback and normal reboot passed");
    return 0;
}
