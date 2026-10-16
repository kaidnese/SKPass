# SKPass CardIO 兼容适配模块

本组件从原 CardIO 核心逻辑延续而来，使用平台无关的 C 接口构造 HID 报告、整理标识并限制发送频率。它不执行 PN7150 的 RF 命令，也不解析卡背 access code。

当前 PN7150 工程由 `main/pn7150.c` 完成激活，由 [skpass_usb.c](../../main/skpass_usb.c)发送 HID；`CardID_Read()` 是保留的辅助接口，不是当前主任务的自动读卡流程。

## 文件职责

| 文件 | 职责 |
| --- | --- |
| `SKPass.c/.h` | 9 字节报告、42 字节 HID 描述符、标识适配与全局节流 |
| `CardID.c/.h` | 调用平台读取回调，先尝试 FeliCa，再尝试 ISO15693 |
| `CMakeLists.txt` | 注册 ESP-IDF 的 `skpass` component |

## 报告与字节顺序

主机实际收到的报告是 **Report ID + 8 字节兼容标识**，共 9 字节。描述符使用厂商页 `0xFFCA`。

| 适配接口 | Report ID | 标识处理 |
| --- | --- | --- |
| `SKPass_FromFeliCa()` | `2` | IDm 保持响应顺序 |
| `SKPass_FromISO14443()` | `2` | UID 截断/补零到 8 字节，首字节执行 `& 0x0F` |
| `SKPass_FromISO15693()` | `1` | 输入必须为原始 LSB-first UID，完全倒序后发送 |

因此 HID 标识不一定等于完整物理 UID，也不是 20 位游戏卡号。某些 USB 栈会自动插入 Report ID，适配回调时需避免重复插入；主机收到的首字节应是 `1` 或 `2`。

USB 枚举由应用层负责。本工程保留 CardIO 的 `VID:PID = 1CCF:5252` 和玩家序列号 **`CARDIOP1` / `CARDIOP2`**，不会随 SKPass 产品名称改成另一组协议标识。

## ESP-IDF 集成

应用组件声明依赖：

```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES skpass)
```

下面仅为接口示例，`platform_*` 由目标平台实现，不是本组件提供的 API：

```c
#include "SKPass.h"

static int send_report(const uint8_t report[SKPASS_REPORT_BYTES], void *ctx)
{
    return platform_send_hid(ctx, report, SKPASS_REPORT_BYTES);
}

/* 在应用初始化位置执行。 */
SKPass adapter;
SKPass_Init(&adapter, send_report, usb_context);
SKPass_SetSuppressTime(&adapter, 3000);

/* 在取得原始 FeliCa IDm 后执行，now_ms 是毫秒计数。 */
SKPass_FromFeliCa(&adapter, idm, now_ms);
```

发送回调返回**负值表示失败，非负值表示接受成功**。若平台 API 用 `false/0` 表示失败，需要将它转为负值，避免组件误认为报告已成功发送。

组件不依赖 Arduino、ESP-IDF 或特定 USB 库，可在其他平台复用。实际硬件接线与当前 USB 队列行为见 [固件 README](../../README.md)。

## 可选的 CardID 读取辅助接口

`CardID_Reader` 接收两个回调：FeliCa 返回响应顺序的 IDm，ISO15693 返回原始 LSB-first UID。回调成功返回 `8`，无卡/失败返回 `0`。

`CardID_Read()` 每次清空结果，先尝试 FeliCa，成功后直接返回；否则尝试 ISO15693。成功结果包含 `type`、`length=8` 和整理后的 `bytes`。

**`CardID_Type` 与 HID Report ID 不是同一组数值。** 应显式映射，不能把 `card.type` 原样当作 Report ID。

`CardID_Read()` 已经倒序整理 ISO15693，因此不要再把它的结果送进 `SKPass_FromISO15693()`，否则会倒序两次。使用处理后的结果时：

```c
CardID_Result card;
if (CardID_Read(&reader, &card)) {
    uint8_t report_id = card.type == CARD_ID_ISO15693
        ? SKPASS_TYPE_ISO15693 : SKPASS_TYPE_FELICA_OR_ISO14443;
    SKPass_Send(&adapter, report_id, card.bytes, now_ms);
}
```

## 重复发送规则

默认抑制间隔 **3000 ms**，可用 `SKPass_SetSuppressTime()` 调整。它是所有卡共享的发送间隔，不是按 UID 永久去重。

只有回调非负返回才更新时间；回调失败可随后再次尝试。抑制期间 `SKPass_Send()` 返回 `0` 且不发送，这个返回值不能单独用来判断报告是否刚刚到达主机。毫秒差使用无符号 32 位减法，支持正常的计数溢出。

本组件不检测物理离卡、不认证或写卡，也不定义 CDC/AimeIO。来源与许可见 [第三方声明](../../../THIRD_PARTY_NOTICES.md)。
