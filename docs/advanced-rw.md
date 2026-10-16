# NFC 高级读写

SKPass v1.0 在上位机「NFC」页面顶部提供并列的「交通卡识别」和「高级读写」入口。选择高级读写后直接展示当前卡片与操作，无需再次展开；两个页面独立滚动，切换保留数据和输入，不发起读写命令。表单只在固件报告了可写的卡型和实际容量后显示；公交卡、银行卡、不符合 Lite-S 条件的 Amusement IC 以及型号/容量未知的卡只显示只读提示。

## 结果保留与全区域扫描（固件 1.0.0）

「高级读写」顶部的表格显示来源 UID、地址、扇区、HEX、ASCII 和读取状态。数据不会因身份过期、重复识别、移卡、换卡或 USB 断线消失。同一张卡的普通读取更新对应地址；成功读取另一张卡时建立新的来源快照。手动清空、开始新扫描或退出程序会清除当前快照，可用「复制全部」或「导出 CSV」保存。保留的数据是历史结果，不代表卡片仍在场。

使用步骤：贴卡 → Classic 选择 Key A/Key B 和密钥 → 点击「扫描全部安全区域」。扫描仅读取，不写卡、不猜测密钥。

| 卡型 | 扫描范围 |
| --- | --- |
| Classic Mini / 1K / 4K | 5 / 16 / 40 个扇区，块 0–19 / 0–63 / 0–255；每组不跨扇区，4K 的块 128–255 每扇区 16 块 |
| NTAG / Ultralight | Page 4 至已确认的最后安全用户页，不扫描锁位或配置页 |
| Lite-S | Scratchpad 块 00–0D，不扫描 MAC/配置区 |
| ISO15693 | 实际容量范围内的 4 字节用户块 |

Classic 所有扇区使用当前所选密钥，密钥不同的扇区标为认证失败。尾块 Key A 和部分 Key B 会被卡片隐藏，读取为零不代表密钥为零。4K 完整扫描是只读扩展，普通读写表单仍保持块 0–63 范围。

每组完成后保留成功单元及失败状态；等待原卡重新激活并取得新 token 后发下一组，最多等待 12 秒。识别到不同 UID、协议、子类型或容量立即停止；确认离场、断线或请求超时也停止并保留数据。「停止扫描」不再发下一组，正在执行的一组返回后仍保留。「重试失败 / 未读项」仅在同一来源卡在场时开放，用当前密钥逐项读取，成功数据不清除。受保护区域仍可能失败，扫描不保证所有扇区可访问。

扫描需要固件能力标识 `-scan-`，旧固件的扫描按钮禁用。CDC/HID 共用 `0x22/0x92` 帧，新增操作 `ADV_SCAN=5`。部分结果中的 `completed` 是连续成功单元数，`length=completed*unit`；Qt 校验身份、请求 ID、操作、地址和长度后才采纳。`OK` 必须完成整组，失败状态仍可携带已经读到的前半组数据。

## 身份绑定

每次真实激活都会生成一个短期目标租约，包含 UID、`protocol`、子类型、地址范围和容量。上位机把同一组字段放入每个 `0x22` 请求；固件在 NFC 任务中重新激活并重新探测，然后再次比较 UID、协议、子类型和容量。卡被拿走、换卡、租约过期、USB 会话改变或重新探测结果不同，响应都是 `STATUS_CARD_MISMATCH`（或明确的断线/射频错误），不会把请求发给另一张卡。USB 回调只入队，RF 交换只由 NFC 任务执行；失败不自动重发。

请求和结果分别使用 `0x22` / `0x92`，目标通知为 `0x13`。CDC 与数据 HID 使用同一帧内容。固定请求头包含请求 ID、token、最多 10 字节 UID、协议、子类型、操作、保护标志、地址、数量、密钥类型、6 字节密钥和数据长度。结果包含原目标身份、完成数量、状态、详情字节和回读数据。单次数据最多 256 字节。

固件目标租期为 1.8 秒，上位机本地兜底租期为 2.2 秒；待执行队列最多等待 2.5 秒。固件只有在下一次真实激活并及时取出请求后才开始重新探测，此时锁住其他写入任务；USB 队列超时不再中断已取出的请求。完成后撤销目标，重新识别才能进行下一次操作。上位机最终结果等待最多 20 秒，超时显示“结果未知”，不假定写入失败或自动重发。

### 二进制负载布局

多字节数值均为小端，UID 按原激活顺序保存并右侧补零。目标帧固定 23 字节：`token[4] uidLen[1] uid[10] protocol[1] subtype[1] first[2] last[2] unit[1] available[1]`。

| 请求偏移 | 字段 | 字节数 |
| --- | --- | --- |
| 0 / 4 | request ID / target token | 各 4 |
| 8 / 9 | UID 长度 / UID | 1 / 10 |
| 19 / 20 / 21 / 22 | protocol / subtype / operation / flags | 各 1 |
| 23 / 25 | 起始地址 / 单元数 | 各 2 |
| 27 / 28 | Key 类型 / Key | 1 / 6 |
| 34 / 36 | 数据长度 / 数据 | 2 / 0–256 |

响应前 21 字节与请求的身份部分一致，偏移 `21/22/23` 为 operation/status/detail，`24/26/28` 为地址/完成数/数据长度（各 2 字节），数据从 `30` 开始。操作 `1/2/3/4/5` 分别为读、写、UID 改写、NDEF、只读扫描；保护位 `1/2/4` 分别为尾块解锁、确认 Gen2、确认 UID 改写。

子类型 `1–9` 依次为 Classic 1K、Classic 4K、Classic Mini、NTAG213、NTAG215、NTAG216、Ultralight、Lite-S、ISO15693 4 字节块；`0` 表示不开放高级读写。状态 `0–10` 依次为 OK、ACCEPTED、CARD_MISMATCH、INVALID、BUSY、AUTH_FAILED、RF_FAILED、VERIFY_FAILED、PROTECTED、DISCONNECTED、DUPLICATE。

## 支持的操作

| 卡型 | 读写范围和校验 |
| --- | --- |
| MIFARE Classic 1K/4K/Mini（`0x80`） | 块 0–63/19，Key A `0x60` 或 Key B `0x61`，预设 FF、Aime/WCCFv2、Bana 或自定义密钥。普通写禁止 Block 0；尾块必须勾选二次确认，并校验访问位反码。Gen2 CUID 的 Block 0 改写必须由用户确认，固件会校验原 UID/BCC/厂商字节并自动计算新 BCC；不会发送 Gen1 后门命令。 |
| NTAG213/215/216、Ultralight（`0x02`） | 用户页从 4 到探测到的最后安全页，单页 4 字节写入；读取按 4 页窗口。可生成标准 URL/Text NDEF，先清空长度，最后发布首页并逐页回读。CC 写访问位不为可写时不开放高级写入。 |
| FeliCa Lite-S（`0x03`、系统码 `88B4`） | 只允许 Scratchpad 块 `00–0D`；读取使用 Service `000B`，写入使用 `0009`，检查 IDm、状态和回读。`0E` 之后的 MAC/配置区域永不触碰。 |
| ISO15693 / ICODE（`0x06`） | 依据 `Get System Information (0x2B)` 的真实块数和 4 字节块大小，使用带 UID 的 Read/Write Single Block，并回读校验。 |

地址、数量、数据长度、UID 长度和子类型在固件和 Qt 端各校验一次。尾块、Block 0、Gen2 UID 和 NDEF 均有独立保护标志；写入前上位机会显示当前 UID 和风险确认框。Classic 尾块回读会隐藏 Key A，因此只验证新 Key A 认证和访问位，界面会明确说明。

## 状态

`OK` 表示操作完成并通过回读；`ACCEPTED` 只表示进入 NFC 任务队列；`CARD_MISMATCH` 表示身份不符；`AUTH_FAILED`、`RF_FAILED`、`VERIFY_FAILED`、`PROTECTED` 和 `DISCONNECTED` 分别保留认证、射频、回读、安全保护和连接故障。写请求有请求 ID 去重，断线、超时或失败不会自动再次写卡。

高级读写不包含交通/银行卡 APDU、支付认证、密钥破解或卡模拟。真实卡的密钥、Gen2 能力、锁位和耦合质量仍需实卡验收；主机测试使用模拟 RF，不代表所有卡片都可写。

## 实现依据与验收

- [ElectronicCats Classic 写块示例](https://github.com/ElectronicCats/ElectronicCats-PN7150/blob/master/examples/MifareClassic_write_block/MifareClassic_write_block.ino)：PN7150 MIFARE tag-command 认证与两阶段写块。
- [NXP MIFARE 扩展](https://github.com/NXPNFCLinux/linux_libnfc-nci/tree/master/src/service/extns/src/mifare)：embedded Key A/Key B 参数；界面的 `60/61` 转为控制器的 `10/90`。
- [nfcpy Sony Type 3](https://github.com/nfcpy/nfcpy/blob/master/src/nfc/tag/tt3_sony.py)：Lite-S IC 类型、Scratchpad 范围、`000B/0009` 服务。
- [NXP NTAG213/215/216 产品页](https://www.nxp.com/products/NTAG213_215_216)：GET_VERSION、READ/WRITE、用户页和配置页布局。

模拟测试覆盖实际协议与 RF 分流、Key B、UID/BCC、尾块访问位、末页窗口、NDEF 发布顺序、写途中换卡、Lite-S 边界和 ISO15693 回读；实际 `skpass_cdc.c` 队列测试覆盖 CDC/HID 对称、换卡、过期、断线会话代数、互斥与慢探测。Qt 测试覆盖目标租期、同目标保留输入、错误响应、确认框内换卡、只读提示与动态范围。

实卡请先选普通用户块读取，核对 UID/型号/容量，再测试可恢复的数据块。对尾块和 Block 0 的验证需使用可承担改写结果的卡；普通正版 Classic 不支持改 UID，不能由成功识别 Classic 推导为 CUID。
