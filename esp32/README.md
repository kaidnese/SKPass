# SKPass ESP32 固件

**这一页讲**：固件做什么、怎么接线、怎么编译烧录、菜单里那 5 个选项是什么、读卡失败时该看什么日志。

当前固件版本 **1.0.0**（来自 `version.txt`），统一发行标识 **v1.0**；上位机 v1.0、AimeIO DLL 接口 1.0。支持 Type 4 NDEF / APDU 卡模拟：`0x23` 控制、`0x93` 响应、`0x14` 交互事件，能力标识包含 `-ce-`。保留 `0x13/0x22/0x92` 高级读写、只读扫描、NFC-B、街机读写和 UF2/ROM 下载。[卡模拟具体实现](../docs/card-emulation.md)；[高级读写说明](../docs/advanced-rw.md)。

智能探测按 ISO-DEP、FeliCa、Type 2、ISO15693 和 Classic 分流。Classic 与已知 Amusement IC 优先完成原有卡号业务；交通类应用仅上报独立详情，不派生街机 Access Code。每次探测至多 40 次交换，开始下一次交换前检查 6.5 秒预算（正在执行的单次交换另有驱动超时），探测失败先尝试 NCI RF 发现恢复。读取结果保存静态 NFC 任务私有文档，避免交易数组挤占任务栈。详见 [协议、布局与限制](../docs/nfc.md)。

**新增 ROM 下载**：正常连接时通过 HID/CDC 发送 `34`，收到 `B4` 后停止读卡并切换到芯片 ROM；支持原生 USB 与 GPIO43/44 UART0。原正常 USB 设备退出，下载设备可能换 COM 号。上位机内置 esptool，校验完成后提示重新插拔 USB，随后自动寻找读卡器。旧固件先通过 UF2 升级，或手动 BOOT + RESET；应用损坏时仍可手动进入 ROM。具体接线、首次烧录／普通更新差异及退出方法见 [固件更新](../docs/firmware-update.md)。

---

## 1. 固件做什么，不做什么

| 做 | 不做 |
|---|---|
| 初始化 PN7150，发现 NFC-A / NFC-B / FeliCa / ISO15693 卡 | **不解析卡背号码**：Classic 扇区、S_PAD0 都是原样上报，解码在电脑端 |
| Classic：读 Sector 0 的 Block 0/1/2（认证 + 有界重试） | 不做 FeliCa 认证读取 |
| FeliCa：只读探测 ID 块 `0x82` 与 S_PAD0 块 `0x00` | 不联网：没有 AiMeDB / `aqua.naominet.live` / TCP 22345 |
| CardIO HID（`1CCF:5252`）+ 双向数据 HID + 兼容 USB CDC | 不提供任意 Reader 指令透传 |
| NFC-A ISO-DEP Type 4 NDEF、UID 与 APDU 规则模拟 | 不模拟 Classic 加密、FeliCa 服务或金融认证 |
| Classic 1K 限额写卡（只写 Block 1/2，回读校验） | 写卡失败不重试、不回滚 |
| 按卡型绑定的 Classic、Type 2、Lite-S 与 ISO15693 高级读写 | 不向交通/银行卡开放写入，不触碰 Lite-S 配置区 |
| UF2 更新（MSC 磁盘）与 ROM 串口下载入口 | 更新不做健康检查自动回滚；不写 eFuse |

> "读到 RF 标识"、"读到完整扇区"、"解析出印号"、"游戏能呈卡" 是四个不同的阶段，别混。

---

## 2. 硬件与接线

目标模块：**ESP32-S3-WROOM-1-N16R8**（16 MB Flash；当前配置未启用 PSRAM）。Flash 为 DIO / 80 MHz。

| 信号 | ESP32-S3 | 说明 |
|---|---|---|
| PN7150 SDA | GPIO4 | I²C0，内部上拉已关闭，**必须外部上拉**（`glitch_ignore_cnt=7`） |
| PN7150 SCL | GPIO5 | 400 kHz，内部上拉已关闭，**必须外部上拉** |
| PN7150 IRQ | GPIO7 | 输入（已启用内部上拉），上升沿中断唤醒；高电平表示有 NCI 数据待处理 |
| PN7150 VEN | GPIO6 | 推挽输出，高电平启用；硬件复位（拉低 20 ms 后拉高等待 100 ms）与 UF2 更新时拉低 |
| GND | 共地 | ESP32、PN7150、外部电源共地 |
| USB D−/D+ | S3 **原生 USB-OTG** | 通常是 GPIO19/20，以开发板原理图为准 |
| UART0 TXD0 / RXD0 | GPIO43 / GPIO44 | 接 3.3V USB 转串口的 RX / TX，用于 ROM 下载；GND 共地 |
| PN7150 供电 | 外部 5 V 路径 | 初始化先读取 `A0 0E`，已有 `06 64 00` 时跳过写入，否则设置外部 5V TVDD；不修改 `A0 0D` |

三个必须注意的点：

1. **I²C 7 位地址是 `0x28`**，不要把线上写/读地址 `0x50/0x51` 当成配置地址。SDA/SCL 的内部上拉是关闭的。
2. **模块能吃 5 V，不代表 ESP32 的 I²C/IRQ/VEN 能承受 5 V。** 带电源电路的成品模块和裸 PN7150 芯片不是一回事。
3. 原生 USB 口用于 HID + CDC、UF2 更新和 ROM USB 下载；板上的 USB-UART 桥可进行 UART0 ROM 下载，并查看 115200 的日志。用串口助手打开应用 CDC 看到二进制是正常的（那是协议帧，不是乱码，也不是 esptool 下载协议）。

---

## 3. 编译与烧录

用**已验证的 ESP-IDF 5.2.5**（ESP-IDF 6.1 未验证）。先激活 IDF 环境，然后在 `esp32/` 目录里：

```powershell
idf.py --version        # 确认是 5.2.5
idf.py build            # 产物：build/SKPass.bin
idf.py -p COMxx flash   # COMxx = ROM 下载口
idf.py -p COMxx monitor # 看日志，Ctrl+] 退出
```

首次部署（或从旧的 `pn7150_cardio` 迁过来）必须**完整烧录 4 个文件**：bootloader、分区表、OTA 初始数据、应用。v1.0 上位机的 ROM 首次烧录模式内置配套文件；也可用 ESP-IDF/esptool。已有 SKPass 双 OTA 布局可 UF2 更新到 1.0.0：旧更新器不含 AutoRestart 能力时首次仍需手动复位，之后自动重启。偏移和操作见 [发行说明](../releases/v1.0/README.md)。

导出到发行目录（在仓库根目录执行）：

```powershell
./scripts/export-firmware.ps1
```

两个提醒：

* 在 VS Code 里打开 `SKPass.code-workspace` 后，ESP-IDF 工程目录要选 **`esp32`**，不要对仓库根目录跑构建。
* 工程里 `esp32/.vscode/settings.json` 指向 IDF 5.2.5；**实际验证过的只有 5.2.5**，首次配置会下载锁定的 TinyUSB 依赖。

---

## 4. 菜单配置（`idf.py menuconfig` → `PN7150 SKPass`）

| 选项 | 默认 | 作用 |
|---|---|---|
| `SKPASS_PLAYER` | `1` | 选序列号 `CARDIOP1` 或 `CARDIOP2`（两台读卡器同机时用） |
| `SKPASS_SUPPRESS_MS` | `3000` | 全局节流：HID 上报与读卡判定的最小间隔（ms） |
| `SKPASS_ENABLE_CDC` | `y` | 保留兼容串口；关闭后仍有 CardIO + 数据两个 HID，原始读卡/写卡/更新不依赖 CDC |
| `SKPASS_FELICA_FULL_SCAN` | `y` | 全量只读扫描 FeliCa 块，用于排查"到底卡不允许还是服务码不对"；会拖慢 FeliCa 读取 |
| `SKPASS_MIFARE_KEY_A` | `574343467632` | Classic 读取用的 Key A，**必须是 12 位十六进制** |

> 读取用的 Key A 和上位机写卡时选的 Key A 是两回事。空白卡的 Key A 通常是 `FFFFFFFFFFFF`，固件在配置的 Key 认证失败时会自动再试一次它。
> 改 `sdkconfig.defaults` 不会覆盖已有的 `sdkconfig`；换 IDF 版本时建议用新的构建目录，避免 CMake/Python 环境混用。

---

## 5. 一次刷卡，固件内部发生了什么

初始化使用保留配置的 `CORE_RESET`，解析 PN7150 的 ROM/FW 版本，吸收复位后的 `60 07` 附加通知，读回核对 TVDD 后下发 `TOTAL_DURATION=256 ms` 和 A/B/F/ISO15693 四项发现配置。I²C 设备句柄首次创建后复用；接收通过 GPIO7 中断信号量等待，没有 1 Tick 的 IRQ 轮询或无卡超时后的额外 10 ms 延时。256 ms 是发现周期配置，不代表所有卡都能在 256 ms 内完成业务读取。

复合卡有多条发现通知时，先收齐本轮列表，再选择 **Classic / FeliCa > T2T > ISO15693 > ISO-DEP > 其他**；相同优先级保留先收到的目标。ISO-DEP 可通过 NFC-A 或 NFC-B 发现。具体命令与测试见 [PN7150 实现](../docs/esp32-implementation.md#6-pn7150-初始化与-i²cnci-收发)。

1. **激活通知**：拿到射频接口、协议、UID/IDm、PMm、SAK，打印
   `CARD_ID (mode=.., protocol=.., interface=..): UID`，并提交到深度为 1 的 HID 发送队列（受 `SKPASS_SUPPRESS_MS` 3000 ms 全局节流控制）。
2. **分派**（若当前有挂起的写卡请求且物理 UID 完全匹配，则优先走写卡流程；否则走读卡流程）：
   * **限额写卡**（仅限确认 `mode=00`、`protocol=0x80`、`interface=0x80`、`sak=08` 且 UID 完全一致的 Classic 1K）：
     1. `stage=1`：用请求携带的 Key A 认证 Sector 0（失败报 `status=6 WRITE_AUTH_FAILED`）；
     2. `stage=2`：**预读（Preflight）** Block 1 与 Block 2（失败报 `status=7 WRITE_PREFLIGHT_FAILED`）；
     3. `stage=3..6`：按 PN7150 两阶段命令（`10 A0 <block>` + `10 <16B 数据>`）依次写入 Block 1（全 `00`）和 Block 2（前 6 字节 `00` + 后 10 字节 BCD，失败报 `status=8 WRITE_FAILED`）；
     4. `stage=7..8`：**逐块回读校验** Block 1 与 Block 2，每通过一块置位 `verified_mask` 对应位（全过为 `0x03`、报 `status=5 WRITE_SUCCESS`，不匹配报 `status=9 WRITE_VERIFY_FAILED`）。
   * **Classic 读卡**（`mode=00` 且 `protocol=0x80`，或 SAK ∈ {`08`,`18`,`09`}）：
     1. 先**免认证**直接读 Block 0/1/2（部分克隆卡允许这么读；这一步不做重试）；
     2. 失败则认证：命令 `40 00 10 <6 字节 Key>`（走 PN7150 的 MIFARE Tag-command，**不是**裸 RF 的 `60/61`）。先试配置的 Key A，认证失败再试一次 `FFFFFFFFFFFF`；
     3. 认证后读块：`10 30 <block>`，成功判据是 `n==18 && rx[0]==0x10 && rx[17]==0`；
     4. 每种 Key 最多 3 次认证尝试、每块最多 3 次读取尝试；每次认证或重试前都会先走 `Pn7150_ReActivate`（`Sleep → Select` 重新选卡并核对 UID/协议/接口一致）以排空残留帧、刷新 Crypto1 与连接信用点；
     5. **只有三块全成功**才上报 48 字节；否则清空数据并报状态 `0x12`/`0x13`；
     6. 每次 READ / 重选 / 认证前都有帧间隔：`vTaskDelay(2)` 在 100 Hz 节拍下对应约 **10~20 ms**。
   * **FeliCa 读卡**（`mode=02` 且 IDm 为 8 字节）：
     1. 读 **ID 块 `0x82`**（拿发行方 DFC，位置是这 16 字节的偏移 8~9）；
     2. 读 **S_PAD0 块 `0x00`**（拿卡号密文，16 字节）；
     3. 服务码按数组顺序试：`0B 00` → `00 0B` → `09 00` → `00 09`；
     4. 只有 `rx[1]==0x07`、`rx[2..9]` 等于 IDm、两个状态字节都为 0、长度匹配，才算读到；
     5. 开了 `SKPASS_FELICA_FULL_SCAN` 时再扫块 `0x00–0x0D` 与 `0x80–0x88`（跳过 `0x0E–0x7F`、`0x00`、`0x82`），每条结果都进日志；
     6. 读不到 S_PAD0 就报状态 `0x11`，**不猜测**。
3. **上报**：`0x10` 卡片事件通过已有 CDC 以及已建立会话的数据 HID 发送；`0x90` 写卡结果只返回请求所属通道。
4. **失败会话丢弃与硬件恢复**：只要 **Classic 读卡失败（`status != 0`）或写卡未成功（`status != 5`）**，主任务会立即跳出当前 RF 循环，打印 `NFC_RECOVERY: ...` 并拉低 VEN 硬件复位 PN7150，绝不把处于半途睡眠或丢失信用点的脏会话留给下一张卡。
5. **等待离场与重新发现**：只有 ISO-DEP（`protocol=0x04, interface=0x02`）卡支持每 500 ms 的 `2F 11 00` 在场检测并在离场时发 `0x11`；其它卡（含 FeliCa、Classic）直接重启 RF 发现准备下一张卡。若 PN7150 初始化失败，每 **2000 ms** 自动重试一次，ESP32 与 USB 始终保持在线。

---

## 6. USB 与协议

正常模式 = **2 个 HID + 1 个可选 CDC**；UF2 模式 = 只有 MSC 磁盘；ROM 模式 = 芯片下载器接管 USB/UART0。TinyUSB 配置需要 `CONFIG_TINYUSB_HID_COUNT=2`，发行默认保留 CDC。

| 通道 | 内容 |
|---|---|
| CardIO HID | VID `1CCF` / PID `5252`，序列号 `CARDIOP1/2`，Usage Page `0xFFCA`。报告描述符声明 **8 字节**内容，主机收到的是 **9 字节**（TinyUSB 自动加 Report ID）：ID `1` = ISO15693，ID `2` = FeliCa/NFC-A/NFC-B |
| 数据 HID | 相同 VID/PID/序列号，独立 Usage Page `0xFFCB` / Usage `1`，Report ID `1`，输入/输出均为 **64 字节**；封装原 `CA 5A` 帧，支持心跳、读数、诊断、写卡、查询和更新 |
| CDC | `CA 5A` + 版本(`01`) + 消息 + 小端长度 + 载荷 + CRC16-CCITT(`0x1021`, 初值 `0xFFFF`)；主机 115200 8N1 且必须拉 DTR |
| MSC | 更新模式，PID 变 `0x5253`，卷标 `SKPASS_UF2`（16 MiB 虚拟 FAT16，内含 `INFO_UF2.TXT` 与 `STATUS.TXT`） |

HID 的 8 字节怎么来的：FeliCa 与 ISO15693（已在 `pn7150.c` 内逆序归一化）要求正好 8 字节（不够就报错不上报）；NFC-A/NFC-B 截断或补零到 8 字节，并把首字节 `&= 0x0F`。

HID 与 CDC 共用的业务消息（数据 HID 分片与握手见 [USB HID 说明](../docs/usb-hid.md)）：

| 方向 | 消息 | 载荷 |
|---|---|---|
| 设备→PC | `01` 心跳 | 固件标识 ASCII，形如 `SKPass-1.0.0-retry-rom-adv-scan-ce-uf2-write`，每秒一次 |
| 设备→PC | `10` 卡片事件 | `type(1) status(1) idm(8) pmm(8) spad_len(1) spad mifare_len(1) mifare`，最多 84 字节 |
| 设备→PC | `11` 已确认离场 | 空 |
| 设备→PC | `12` NFC 详情 | 单片不超过 512 字节的 JSON；`scan + uid + part + count` 组成一个摘要与最多 10 条交易的文档，见 [NFC 协议](../docs/nfc.md) |
| 设备→PC | `13` 高级目标 | 当前 UID、协议、子类型、token 与安全地址范围；只有真实激活才刷新租约 |
| PC→设备 / 设备→PC | `22` / `92` 高级读写 | 类型化请求与回读结果，固件重新核对身份与范围；CDC/HID 对称，见 [高级读写协议](../docs/advanced-rw.md) |
| 设备→PC | `7F` 诊断 | UTF-8 文本（`MIFARE`、`FELICA`、`TAG`、`CARD`、`NFC_RECOVERY` 行） |
| 设备→PC | `90` 写卡结果 | 固定 51 字节：`id(4) status(1) stage(1) detail(1) uid_len(1) uid(10) verified_mask(1) blocks(32)` |
| 设备→PC | `B0` / `B1` | 更新请求回复（1 字节：`0` 接受并 500 ms 后重启、`1` 写卡忙拒绝、`2` OTA 不可用）/ 设备信息 JSON |
| PC→设备 | `20` 写卡请求（31 字节）/ `30` 进入 UF2（空载荷）/ `31` 查询设备信息（空载荷） | 其它消息一律忽略 |
| PC→设备 / 回复 | `34` / `B4` | ROM 请求一字节：0 USB、1 UART0；回复 0 接受、1 写卡忙、2 请求冲突/失败、3 eFuse 禁用 |

* `0x10` 里的 `type`：`0x10` Classic、`0x20` FeliCa、`0x30` ISO15693；
  `status`：`0x00` 成功、`0x11` 无 S_PAD0、`0x12` 认证失败（含 Key 格式非法）、`0x13` 读块/重选/会话失败、`0x14` PMm 不可用、`0x15` 不是 MIFARE Tag-command 接口、`0x16` 暂不支持。
* `0x90` 里的写卡 `status`：`1` 已接受排队、`2` 排队满 15 秒超时过期、`3` 请求非法或卡型/UID 不匹配被拒、`4` 已有未完成写卡请求（忙）、`5` 写入并回读校验成功、`6` 扇区认证失败（`stage=1`）、`7` 写前预读失败（`stage=2`）、`8` 两阶段写块失败（`stage=3..6`）、`9` 回读校验失败（`stage=7..8`）、`10` 写卡挂起期间所属通道断开（CDC DTR 掉线，或 HID 会话关闭/过期/替换，黏性作废；快速重连亦不会复生旧请求）。

队列与节流的真实行为：卡事件队列深度 1（**新事件覆盖旧事件**）、诊断队列 96（满则丢弃并计数）、回复队列 16、待写队列 1（有效期 **15 秒**，且拒绝与上一次 `id` 相同的重复写请求）。CDC 未连接时不发送、清空解码缓存、将挂起的待写请求标记为断开；连接后先发一行裸文本 `<固件标识> ready\r\n`，随后恢复二进制心跳。

上段是兼容 CDC 的队列；数据 HID 独立使用事件队列 1、诊断队列 32、回复队列 16、接收队列 16×64 字节。两个通道共用同一个 I/O 任务和待写队列，NFC 操作仍只在原 NFC 任务执行。数据 HID 需会话握手，主机每秒保活，固件租期 3 秒；协议与端点分配见 [USB HID 说明](../docs/usb-hid.md)。

CDC 和数据 HID 各有独立的 NFC 详情队列（深度 11）。新扫描会替换尚未发出的旧详情，电脑端按扫描序号丢弃过期文档。发送优先级为回复、心跳、诊断、原卡片事件、NFC 详情，保证原有业务先发送；详情不占用旧卡片事件的单项覆盖队列。

---

## 7. UF2 固件更新

* 上位机发 `0x30` → 固件若未处于写卡忙状态，先回 `0xB0`（状态 `0`），延迟 **500 ms** 将 `RTC_NOINIT_ATTR` 标志位设为 `0x43554632` 并软重启进入 MSC 模式（PID `0x5253`），同时拉低 VEN 禁用 PN7150。
* **单次触发（One-shot）**：启动时读取完 `RTC_NOINIT_ATTR` 标志位后立即将其清零；若在更新模式下未刷入新固件就直接断电或按硬件复位键，重新上电后会自动恢复正常读卡模式。
* 只接受本工程格式的 UF2：family `0xC47E5767`、flag `0x2000`、每块 512 字节、payload 256 字节、地址必须等于 `block_no * 256`（应用相对偏移，最大 6 MiB）；支持相同内容的重复块幂等忽略，拒绝冲突重复块或混杂块数。
* 写入**非活动 OTA 槽**（16 MB Flash 下包含两个 6 MiB OTA 槽），收齐全部块后执行 `esp_ota_end` 校验镜像校验和/SHA-256，并核对镜像内 `esp_app_desc_t.project_name` 必须为 `"SKPass"` 或 `"pn7150_cardio"`，通过后才切换启动分区。
* 更新成不成功不看电脑的复制进度，要看磁盘里的 `STATUS.TXT`：`READY` → `COMPLETE` 才是成功，`ERROR:` 会中止 OTA 并保留旧固件。
* 当前固件更新器先准备重启任务再切换启动分区，失败保留旧启动项。成功后发布 `COMPLETE`，电脑读到状态后等约 2 秒，主动断开 USB 再 `esp_restart()`；未读取状态时约 17 秒后重启。清空更新标志，下一次启动恢复读卡。分区切换后忽略剩余 OS 写入，不将已提交成功误改为失败。
* `INFO_UF2.TXT` 含 `AutoRestart` 能力行，上位机据此自动重连；旧更新器没有此行，仍提示手动复位。第一次用旧更新器装入新固件不会改变旧更新器的行为。
* 没有独立恢复 bootloader，也没有自动回滚。失败或未完成更新可按硬件复位键退出；没有复位键时断开 USB **和外部 5 V** 再上电。

---

## 8. 读不到卡时看什么

| 现象 | 看哪里 |
|---|---|
| 完全没有日志 | PN7150 供电/接线；`idf.py monitor` 是否接的是 USB-UART 口 |
| 反复 `NFC_RECOVERY` | 初始化或 NCI 收发失败，多半是 I²C 干扰、天线或供电 |
| `CARD_ID` 有，但事件 `status=0x12` | 认证失败。换 `SKPASS_MIFARE_KEY_A`（空白卡 `FFFFFFFFFFFF`）、卡摆正、重刷。**不能判断成空白卡** |
| `status=0x13` | 读块或重选失败（已重试 3 次）。`MIFARE RETRY exhausted` 表示软件容错用尽 |
| `status=0x11` | FeliCa 没读到 S_PAD0。开 `SKPASS_FELICA_FULL_SCAN`，看 `FELICA ... status1/status2`：`A7` = 该块访问不允许（多半要认证），`A6` = 服务码不对 |
| `raw=100400` / `10B0` 且 `result=ESP_OK` | 只是收到了响应，**不等于读块成功**，要看后面的状态字节 |
| 同一张卡要等一会儿才能再刷 | `SKPASS_SUPPRESS_MS` 默认 3 秒的全局节流，不是故障 |
| `status=0x15` | 这张卡没走 MIFARE Tag-command 接口，读不了扇区 |

诊断行样例：`MIFARE block=01 result=ESP_OK raw=... attempt=1/3 start_ms=...`、`MIFARE RETRY block=01 attempt=2/3 action=reselect_reauth`、`FELICA idm=... service=0B00 block=82 status1=00 status2=00 result=ESP_OK data=...`。

---

## 9. 源码导航

| 文件 | 职责 |
|---|---|
| `main/main.c` | NFC 主任务、读/写调度与恢复状态机 |
| `main/pn7150.c` | I²C/NCI、激活解析、Discovery、Tag-command 收发、重选 |
| `main/aime_reader.c` | 卡型判别、Classic 调度、FeliCa 只读探测 |
| `main/smartcard_probe.c/.h` | 智能 NFC 只读探测、交通/EMV/FeliCa/存储卡解析、有界交换与分片 JSON 编码 |
| `main/mifare_reader.c` | Classic 认证、读块、帧间隔与有界重试 |
| `main/card_writer.c`、`writer_protocol.c` | 限额写卡与报文 |
| `main/card_emulation.c/.h`、`ce_protocol.c/.h` | Type 4 NDEF 文件、APDU 本地规则、分段写入提交、CE 控制与事件协议 |
| `main/skpass_usb.c`、`skpass_cdc.c` | 两个 HID/兼容 CDC 描述符、共享 I/O 任务、会话、队列与请求处理（保留旧文件名减少改动） |
| `main/hid_wire.c/.h` | 平台无关的 64 字节数据 HID 分片与重组，电脑端共用 |
| `main/reader_frame.c` | v1 帧与 CRC16 |
| `main/uf2_update.c` | MSC 磁盘、UF2 接收与 OTA 校验 |
| `main/rom_download.c` | eFuse 能力检查、ROM 请求、VEN 关闭、USB 断开、强制下载与系统复位 |
| `components/skpass/` | 平台无关的 CardIO HID 适配（`CardID_Read()` 是保留接口，主流程没调用） |

---

## 10. 测试与验证边界

* `tests/` 包含 15 项主机模拟测试：既有 CardIO/CRC、MIFARE、写卡、HID、UF2、ROM、公开信息与高级读写，加 Type 4 引擎和 PN7150 Listen/信用/分片测试。覆盖中断竞态、句柄复用、TVDD、发现优先级、NDEF 提交、CE/RW 互斥与断线会话取消。见 [验证记录](../docs/verification.md) 与 [卡模拟](../docs/card-emulation.md)。
* 已通过：ESP-IDF 5.2.5 / ESP32-S3 / 16 MB 构建，分区大小检查，镜像 SHA256/校验和，主机模拟测试。
* 2026-10-13 实机验证：原生 USB ROM 下载已成功将 2.7.1 写入备用分区，应用与启动记录通过摘要校验；手动恢复后查询确认版本与 NFC ready。ROM 自动复位未能稳定恢复 USB，按当前方案取消，上位机改为提示重新插拔 USB。
* **未验证**：外接 UART0 下载、真实 RF 距离、卡片写入、UF2 更新、USB 背压、游戏侧兼容性。软件重试修不了天线耦合。详细边界见验证记录。

来源与许可见 [第三方声明](../THIRD_PARTY_NOTICES.md)。
