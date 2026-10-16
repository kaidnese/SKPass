#ifndef I2C_SCAN_TASK_H
#define I2C_SCAN_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief I2C 扫描任务函数
 *
 * 这个任务会持续扫描 I2C 总线上的设备，并通过 ESP_LOG 输出结果
 *
 * @param pvParameters 任务参数（未使用）
 */
void i2c_scan_task(void *pvParameters);

#endif // I2C_SCAN_TASK_H
