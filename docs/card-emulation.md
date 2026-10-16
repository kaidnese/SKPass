# PN7150 卡模拟

配套版本：上位机 **v1.0**、固件 **1.0.0**。入口为 **NFC → 卡模拟**，与交通卡识别、高级读写并列。

## 如何使用

1. 先更新固件，连接 SKPass，进入「卡模拟」。连接后页面自动查询当前 NFC 模式，不自动开启模拟。
2. 输入 8 个十六进制字符作为 NFC-A 的 4 字节 UID，或点「随机 UID」。这是射频标识，与街机 Access Code 无关。
3. 选择网页链接或 UTF-8 文本，填写内容。网页仅接受 HTTP(S) 地址；页面显示 NDEF 消息占用的字节数，容量为 240 字节。
4. 根据需要勾选「允许外部设备写入 NDEF」。关闭时 CC 文件声明只读，UPDATE BINARY 返回 `6982`。
5. 点「应用配置并开启卡模拟」。收到固件成功结果后，页面和连接栏显示「正在模拟卡片」。等待手机靠近天线。
6. 使用 NFC Tools 等软件读取或写入标签。系统是否自动弹出网页、是否直接显示文本取决于手机系统与 NFC 应用；纯文本记录通常需要 NFC 应用查看。
7. 点「切回读卡器模式」，成功后恢复街机卡读取、CardIO 输出和高级读写。查询按钮可确认设备实际模式。

UID、NDEF 类型与内容会保存到上位机偏好设置。应答规则和写入权限在当前页面中保留，需在重新启动上位机后重新设置。固件配置及手机写入的内容只保存在 RAM，PN7150 射频故障恢复期间保留；ESP32 重启后回到读卡模式，需再次应用配置。关闭上位机不会主动停止已经开启的模拟，重新连接后可查询、关闭。

## 自定义 APDU

最多配置 4 条规则，按表格顺序匹配。支持完整匹配或前缀匹配；匹配字节为 1–16 字节，应答为 2–32 字节，必须自行带上 SW1/SW2。例如匹配 `80 CA`，应答 `01 02 90 00`。

内置 NDEF 指令优先处理，其余指令匹配自定义规则；未匹配时返回所选的 `6A82` 或 `9000`，默认 `6A82`。规则提前下发并在固件本地应答，射频交互不等待电脑往返。APDU 接收/发送、激活/离场和控制器错误带序号上报，在抓包框持续显示，最多保留 2000 行。USB 事件队列满时允许丢日志以保证 NFC 应答，固件诊断记录丢帧数；这不是无损抓包设备。

## 固件实现

USB 任务只解码、校验与排队。原 NFC 任务独占所有 PN7150 I²C 操作，切换时清除旧目标身份并硬件复位 PN7150，不重启 ESP32，不重新枚举 USB。

Listen 初始化依次执行：

```text
CORE_RESET（保留配置） → CORE_INIT → 专有接口激活 → A0 0E 读回比对
CORE_SET_CONFIG：32=20，33=<4 字节 UID>
CORE_RESET（保留配置） → CORE_INIT → 专有接口激活
RF_DISCOVER_MAP：21 00 04 01 04 02 02
RF_SET_LISTEN_MODE_ROUTING：21 01 07 00 01 01 03 00 01 04
RF_DISCOVER：21 03 05 02 80 01 82 01
```

设置 UID 后保留配置的 RESET/INIT 使新 Listen 配置生效。没有修改 `A0 0D`，没有加入产测命令。`A0 0E` 沿用外部 5V 的读回比对逻辑。

`RF_INTF_ACTIVATED_NTF` 必须是 ISO-DEP 接口、ISO-DEP 协议、Listen 模式。驱动从激活通知取得最大数据负载与信用计数，消耗 `CORE_CONN_CREDITS_NTF`，重组 NCI DATA 分片，按最大负载拆分 APDU 应答。丢失分片或等待信用超时会放弃射频会话并重新初始化 Listen，绝不把部分 APDU 当作完整指令。

`RF_DEACTIVATE_NTF` 清除应用/文件选择与未完成写入。Discovery 类型保持监听，Idle 重新下发 Listen discovery，Sleep 尝试返回 Discovery；恢复失败走 Listen 模式的重新初始化，不调用 Reader 模式的发现配置。回到 RW 则重新初始化原有轮询和协议映射，NFC-B 保留。

## Type 4 NDEF 文件

`card_emulation.c` 是不依赖 ESP-IDF 的引擎，支持短 APDU：

| 指令 | 行为 |
| --- | --- |
| SELECT AID `D2760000850101` | 选择 NDEF 应用，清除本次文件选择 |
| SELECT FILE `E103` | 选择 15 字节 CC，声明 Mapping 2.0、NDEF 文件 `E104` |
| SELECT FILE `E104` | 选择 NDEF 文件，最大长度 242 字节（NLEN 2 + 消息 240） |
| READ BINARY | 根据偏移和 Le 返回选中文件，越界拒绝；读到文件尾不足 Le 返回 `6282` |
| UPDATE BINARY | 仅在选择 NDEF 文件且允许写入时处理；校验偏移、Lc 与容量 |

典型外部写入顺序：NLEN 写零 → 分段写消息 → 写最终 NLEN。写零后标签可见长度为零，页面报告清空；最终非零 NLEN 只有在所有字节均已接收且 NDEF 格式有效时才提交并上报。中途移开不会暴露半段消息。也接受单条 UPDATE 中包含完整 NLEN 与完整 NDEF 的写入。

URL 使用 NFC Forum RTD URI（支持压缩 HTTP/HTTPS 前缀），文本使用 UTF-8 RTD Text。上位机显示外部写入的常见 URL/UTF-8 文本；其他合法 NDEF 记录以原始十六进制显示。UI 的外部写入结果、APDU 日志在离场和断线后保留。

## CDC/HID 协议

继续使用原有 `CA 5A 01 msg lenLE payload CRC16` 封包；负载上限 512 字节。CDC 流式传输与数据 HID 分片共用同一解码器和控制执行流程。

| 消息 | 方向 | 内容 |
| --- | --- | --- |
| `23` | PC → 固件 | 请求 ID、动作（0 查询 / 1 RW / 2 应用 CE），CE 配置 |
| `93` | 固件 → PC | 请求 ID、结果、当前模式、UID、错误码 |
| `14` | 固件 → PC | schema=1、事件类型、递增序号、UID、事件数据 |

请求 ID 为非零 uint32 LE。查询/RW 请求为 5 字节。CE 应用请求依次为：ID4、动作1、UID4、允许写入1、NDEF 长度2 LE、NDEF、兜底状态码2、规则数量1；每条规则为前缀标志1、匹配长度1、应答长度1、匹配字节、应答字节。最大 459 字节。

响应固定 12 字节：ID4、状态1、模式1、UID4、错误2 LE。状态：0 成功、1 已接受、2 无效、3 忙、4 失败、5 请求会话已断开。模式：0 RW、1 Listen、2 切换中、3 故障。`ACCEPTED` 仅表示排队，只有最终成功结果才能确认模式；ID=0 用于控制器恢复后的模式通知。排队超过 5 秒或主机会话已更换时取消执行，不自动重发。

事件头 10 字节：版本1、类型1、序号4 LE、UID4。类型：1 激活、2 离场、3 RX APDU、4 TX APDU、5 新 NDEF、6 错误。APDU/NDEF 为原始字节。两种传输对称支持，事件同时发往有效连接。

CE 或切换期间固件拒绝原有写卡/高级读写请求，上位机也禁用相关操作；UF2/ROM 下载依然可用，但等待正在进行的模式切换结束。USB 心跳持续有效；按需设备信息中的 `nfc_state` 可显示 `card_emulation`。

## 能力边界与验证

这是 **NFC-A / ISO-DEP Type 4 NDEF 与自定义 APDU 模拟**。发现命令也保留 NFC-F Listen，但本引擎不提供 FeliCa 服务/IDm 模拟；ISO-DEP NDEF 不等于 FeliCa Type 3 模拟。自定义 UID 不提供 Classic Crypto1、交通卡密钥、银行卡签名或支付应用，实现不能用来复制那些卡片的认证能力。PN7150 必须由 ESP32/USB 持续供电。

已通过固件与 Qt 编译、引擎/驱动模拟测试、真实 CDC/HID 封包与 Qt 页面测试。尚未烧录本版本或测试真实手机，因此手机射频兼容性、实际 UID 呈现和 NFC Tools 的完整写入流程须实机确认。首次验收建议依次验证 URL 读取、文本读取、外部写入、移开后重读、自定义 APDU、切回 RW 原有卡号读取。

参考实现：[ElectronicCats-PN7150](https://github.com/ElectronicCats/ElectronicCats-PN7150) 的 `configMode`（`MODE_CARDEMU`）、`configureSettings`、`StartDiscovery` 和 [T4T_NDEF_emu.cpp](https://github.com/ElectronicCats/ElectronicCats-PN7150/blob/master/src/T4T_NDEF_emu.cpp)。SKPass 使用独立的有界解析和写入提交实现，没有照搬上游的无界 APDU 下标访问。
