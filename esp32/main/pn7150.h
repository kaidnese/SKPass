#ifndef PN7150_H
#define PN7150_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Optional cancellation check, installed by the NFC task owner. */
void Pn7150_Set_Interrupt_Check(bool (*check)(void));
#include "esp_err.h"
#include "driver/i2c_master.h"

// PN7150 I2C 地址
// 7位地址: 0x28
// 8位写地址: 0x50 (0x28 << 1 | 0)
// 8位读地址: 0x51 (0x28 << 1 | 1)
#define PN7150_I2C_ADDRESS      0x28
#define PN7150_I2C_WRITE_ADDR   0x50
#define PN7150_I2C_READ_ADDR    0x51

// I2C pins
#define I2C_SCL_GPIO            5    // SCL
#define I2C_SDA_GPIO            4    // SDA
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000

// GPIO output pin
#define GPIO_VEN_PIN            6

// GPIO IRQ Pin
#define GPIO_IRQ_PIN            7

#define PN7150_NFCID_MAX_LENGTH 10

typedef struct {
    uint8_t discovery_id;
    uint8_t rf_interface;
    uint8_t protocol;
    uint8_t mode_technology;
    uint8_t uid[PN7150_NFCID_MAX_LENGTH];
    size_t uid_length;
    uint8_t sak;
    bool sak_valid;
    uint8_t pmm[8];
    bool pmm_valid;
    uint8_t system_code[2];
    bool system_code_valid;
} pn7150_card_info_t;

/**
 * @brief 初始化 PN7150 模块
 *
 * 配置 VEN 和 IRQ GPIO，安装上升沿 ISR 与二值信号量，并保持 VEN 为高电平。
 * 可重复调用；所有 NCI 收发仍由 NFC 任务串行执行。
 */
void Pn7150_Init(void);

/**
 * @brief 唤醒 PN7150 模块
 *
 * 保持 VEN 为高电平，不执行复位
 */
void Pn7150_WakeUp(void);

/**
 * @brief 硬件复位 PN7150，清除 RF 会话与数据流控状态
 *
 * VEN 拉低 20 ms，拉高后等待 100 ms；不会复位 ESP32 或 USB。
 */
void Pn7150_Reset(void);

/**
 * @brief 测试 PN7150 是否在 I2C 总线上响应
 *
 * @param bus_handle I2C 主机总线句柄
 * @return true - PN7150 响应 ACK，设备在线
 *         false - PN7150 无响应
 */
bool Pn7150_Test_ACK(i2c_master_bus_handle_t bus_handle);

/**
 * @brief 发送 NCI CORE_RESET_CMD
 *
 * @param bus_handle I2C 主机总线句柄
 * @return ESP_OK - 命令已发送；其他值 - 发送失败
 */
esp_err_t Pn7150_Send_Core_Reset(i2c_master_bus_handle_t bus_handle);

/**
 * @brief 等待 IRQ 并读取 CORE_RESET_RSP
 *
 * @param bus_handle I2C 主机总线句柄
 * @return ESP_OK - 响应头校验通过；其他值 - 读取或校验失败
 */
esp_err_t Pn7150_Read_Core_Reset_Response(i2c_master_bus_handle_t bus_handle);

/**
 * @brief 发送 NCI CORE_INIT_CMD
 *
 * @param bus_handle I2C 主机总线句柄
 * @return ESP_OK - 命令已发送；其他值 - 发送失败
 */
esp_err_t Pn7150_Send_Core_Init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief 等待 IRQ 并读取 CORE_INIT_RSP
 *
 * @param bus_handle I2C 主机总线句柄
 * @return ESP_OK - 响应头校验通过；其他值 - 读取或校验失败
 */
esp_err_t Pn7150_Read_Core_Init_Response(i2c_master_bus_handle_t bus_handle);

/** @brief 执行完整 NCI 初始化并启动 RF 轮询 */
esp_err_t Pn7150_Initialize(i2c_master_bus_handle_t bus_handle);

/**
 * @brief 等待 RF_INTF_ACTIVATED_NTF 并解析卡片信息
 *
 * @param bus_handle I2C 主机总线句柄
 * @param card_info 输出卡片信息，可直接交给后续 USB 上报层
 */
esp_err_t Pn7150_Read_Activated_Notification(i2c_master_bus_handle_t bus_handle,
                                             pn7150_card_info_t *card_info);

/** @brief 等待当前 ISO-DEP 卡片离开射频场 */
esp_err_t Pn7150_Wait_Card_Removed(i2c_master_bus_handle_t bus_handle,
                                   const pn7150_card_info_t *card_info);

/** @brief 从 Idle 状态重新启动 RF 发现 */
esp_err_t Pn7150_Start_Discovery(i2c_master_bus_handle_t bus_handle);

/** @brief 结束当前卡片连接并重新启动 RF 发现 */
esp_err_t Pn7150_Prepare_Next_Card(i2c_master_bus_handle_t bus_handle);
esp_err_t Pn7150_ReActivate(i2c_master_bus_handle_t bus_handle,
                          const pn7150_card_info_t *card);

/* Serialized with all other NCI I/O by the NFC task. rx includes the
 * controller status byte on Frame/Tag-command interfaces. */
esp_err_t Pn7150_Tag_Transceive(i2c_master_bus_handle_t bus_handle,
                              const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t capacity, size_t *rx_len);

/* Card-emulation Listen mode. All calls are serialized by the NFC task. */
esp_err_t Pn7150_Initialize_Card_Emulation(i2c_master_bus_handle_t bus_handle,
                                           const uint8_t uid[4]);
/* kind: 1=RF_INTF_ACTIVATED_NTF, 2=NCI data, 3=RF_DEACTIVATE_NTF. */
esp_err_t Pn7150_Card_Emulation_Receive(i2c_master_bus_handle_t bus_handle,
                                        uint8_t *data, size_t capacity,
                                        size_t *length, uint8_t *kind,
                                        uint32_t timeout_ms);
esp_err_t Pn7150_Card_Emulation_Send(i2c_master_bus_handle_t bus_handle,
                                     const uint8_t *data, size_t length);

#endif // PN7150_H
