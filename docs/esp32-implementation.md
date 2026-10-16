# SKPass ESP32 固件：实现说明与开发记录

SKPass 是基于 **ESP32-S3 + PN7150** 的 USB 读卡器项目。`esp32/` 是可独立构建的 ESP-IDF 固件工程；上位机、AimeIO DLL、HID 测试工具不再混在固件目录中。

本文详细章节保留 **2026-10-11 / 固件 2.5.0** 的实现基线，结合项目开发对话中的需求、日志和问题整理。历史日志用于解释设计原因，不代表旧问题在所有硬件上都已验证解决。文中区分“代码已实现”“软件测试通过”和“需要实机验证”；源码与历史讨论冲突时，以当前源码为准。

> [!NOTE]
> **正式发布说明**：当前统一发行版本为上位机 **v1.0**、固件 **1.0.0**、AimeIO **1.0**。下文保留了开发阶段各特性的加入过程与历史基线说明，供技术深入排查参考。

**2026-10-13 更新：** 2.6.0 加入独立数据 HID（`FFCB:1`），正常模式为两个 HID + 可选 CDC；Qt 与 AimeIO 优先 HID，未发现才回退 CDC。加入 UF2 自动重启与 `rom_download.c`：收到 HID/CDC `34` 后暂停 NFC、关闭 VEN、退出 TinyUSB，设置 RTC 强制下载并系统复位；上位机以无 stub 的 esptool 完成 USB/UART0 下载和校验，提示重新插拔 USB，不自动复位。增加 USB 启动时 500ms 断开再连接。完整流程、官方依据见 [更新说明](firmware-update.md)，协议与测试见 [USB HID](usb-hid.md) 和 [验证记录](verification.md)。下面单 HID、仅 CDC、手动 UF2、旧哈希及 v1.4 路径保留为历史基线，当前连接/发行以这些补充文档为准。

**同日 PN7150 驱动升级：** GPIO7 中断唤醒、400 kHz I²C 与持久设备句柄、多协议发现优先级、A/B/F/V 轮询与 256 ms `TOTAL_DURATION`、TVDD 读回比对及芯片固件版本解析。按用户后续要求保留 NFC-B 主动轮询。NCI Reset 保留已有配置，避免恢复时重写相同 TVDD；不改 `A0 0D`、公开接口或读写卡业务。此构建仍标为 2.7.1，生成文件在 `esp32/build/SKPass.bin`，未替换 `releases/v1.0` 的旧镜像；详见第 6、7 节和验证记录。

阅读入口：[固件 README](../esp32/README.md)提供接线、构建和快速排障；[卡型与 access code](access-codes.md)说明电脑端卡号解析及 AimeIO 呈卡。本文保留完整固件实现和此前开发问题，按目录选择章节即可。

## 目录

- [1. 功能范围与重要边界](#1-功能范围与重要边界)
- [2. 工程目录与模块职责](#2-工程目录与模块职责)
- [3. 硬件、接线与供电](#3-硬件接线与供电)
- [4. 编译配置与首次烧录](#4-编译配置与首次烧录)
- [5. 启动、任务与恢复状态机](#5-启动任务与恢复状态机)
- [6. PN7150 初始化与 I²C/NCI 收发](#6-pn7150-初始化与-i²cnci-收发)
- [7. 卡片激活、UID 与协议判别](#7-卡片激活uid-与协议判别)
- [8. Classic 读取、认证与有界重试](#8-classic-读取认证与有界重试)
- [9. FeliCa 读取与 access code 的边界](#9-felica-读取与-access-code-的边界)
- [10. 移卡、下一张卡与同卡重复刷入](#10-移卡下一张卡与同卡重复刷入)
- [11. CardIO 兼容 HID](#11-cardio-兼容-hid)
- [12. CDC 串口协议与队列](#12-cdc-串口协议与队列)
- [13. 限定 Classic 1K 写卡](#13-限定-classic-1k-写卡)
- [14. 按需获取设备信息](#14-按需获取设备信息)
- [15. 真正的 UF2 MSC 固件更新](#15-真正的-uf2-msc-固件更新)
- [16. 对话中问题的解释与排障](#16-对话中问题的解释与排障)
- [17. 测试、验收与后续开发](#17-测试验收与后续开发)
- [18. 参考资料、历史文档与许可](#18-参考资料历史文档与许可)

## 1. 功能范围与重要边界

### 1.1 当前实现

| 功能 | 当前实现 | 主要限制 |
| --- | --- | --- |
| PN7150 初始化 | I²C ACK、NCI Reset/Init、供电配置、RF 映射、RF Discovery | 面向当前 PN7150/NCI 路径，不能直接换成 PN7160 |
| 卡片标识 | NFC-A UID、NFC-B 标识、FeliCa IDm、ISO15693 UID | UID/IDm 不等于卡背 access code |
| UART 日志 | `ESP_LOGI` 输出 `CARD_ID`、命令和读取诊断 | 原生 CDC 不是 UART 控制台 |
| CardIO 兼容 HID | 保留 9 字节报告、Report ID、VID/PID、玩家序列号 | 只传兼容标识，不传完整扇区/access code |
| USB CDC | 心跳、原始读卡数据、诊断、写卡结果、信息查询、更新命令 | 只有一个 CDC 通道；PC 程序可能互相占用端口 |
| MIFARE Classic 读取 | 扇区 0 的 Block 0、1、2；Key A 认证及 FF 回退 | 不扫描全卡，不读取 Block 3，不破解未知密钥 |
| Classic 容错 | 帧间隔、认证与单块有界重试、Sleep/Select 重选、VEN 恢复 | 不保证改善严重弱耦合；重选失败立即中止软重试 |
| FeliCa 读取 | IDm/PMm；只读服务/块探测；取得原始 S_PAD0 | 固件不解码印号，不执行认证或密钥写入 |
| 连续刷卡 | 每次激活可再次提交，RF 会话结束后重新 Discovery | 默认全局 3000 ms 节流；不是所有协议都有物理离卡检测 |
| Classic 1K 写卡 | 仅 Block 1、Block 2；UID 绑定、预读、回读比较 | 不改 UID、Key A/Key B、访问位或扇区尾块 |
| 设备信息 | 主机 `31` 查询，固件 `B1` 返回 JSON | 仅查询时快照；不自动连续上报详情 |
| 固件更新 | 原生 USB MSC、虚拟 FAT16、UF2 写入非活动 OTA 分区 | 需先完整烧录；不是任意 UF2/任意 BIN 都可更新 |

### 1.2 明确没有实现的功能

- ESP32 不连接 `aqua.naominet.live` 或官方 AiMeDB，不建立 TCP 22345 连接，也不负责账号注册、绑定或查询。当前配套 Qt 上位机和 AimeIO DLL 也没有实现该查询；此前讨论的联网方案尚未加入代码。
- ESP32 不加载 `aimeio.dll`，也没有所谓“AimeIO USB 接口”。AimeIO 是游戏进程加载的 PC 端 DLL，消费本固件的 CDC 数据。
- ESP32 不管理虚拟卡 JSON、最近 10 条记录、备注、键盘长按刷卡、segatools.ini 或 DLL 位数。它们属于 `../desktop/` 和 `../aimeio/`。
- 不把所有 UID 当成 20 位卡号，不把 IDm 派生号标成已经核对的卡背印号，不仅凭 RF 协议断言发行方。
- 不通过修改 RF 参数保证某个读卡距离；不写入未经目标天线测量验证的 `A0 0D` 配置。
- 不提供任意 NCI 命令、任意扇区写入、UID 克隆或密钥破解的 CDC 接口。

因此，**CardIO HID、CDC 和 PC 端 AimeIO 可以保留在同一套系统中**，但并不意味着上位机与游戏能同时独占同一个串口。

## 2. 工程目录与模块职责

SKPass 根目录的组织方式：

```text
SKPass/
├─ esp32/                  本固件源码与配置
├─ desktop/                Qt 上位机源码
├─ aimeio/                 x86/x64 游戏 DLL 源码
├─ host-test/              CardIO HID 测试工具
├─ docs/                   专项说明及历史记录
├─ scripts/                构建、打包、固件导出脚本
└─ releases/v1.0/          固件 BIN、上位机 EXE、x86/x64 DLL
```

固件内部：

| 文件/目录 | 职责 |
| --- | --- |
| `CMakeLists.txt` | ESP-IDF 工程入口，要求 ESP32-S3，工程名 `SKPass` |
| `version.txt` | 应用版本来源，当前 `1.0.0` |
| `sdkconfig` / `sdkconfig.defaults` | 当前配置及默认配置；默认文件不覆盖所有已有配置 |
| `partitions.csv` | 16 MB Flash、双 6 MiB OTA 分区 |
| `main/main.c` | I²C 初始化、NFC 主任务、读写调度、错误恢复、下一张卡 |
| `main/pn7150.c/.h` | GPIO、NCI 控制命令、DATA 流控、激活解析、重选、Discovery |
| `main/aime_reader.c/.h` | 卡型分支、FeliCa 只读、Classic 读取、CDC 事件显式编码 |
| `main/mifare_reader.c/.h` | Classic 认证、Block 0～2、间隔和重试 |
| `main/skpass_usb.c/.h` | 正常 HID+CDC/纯 HID 描述符、MSC 描述符、HID 待发送队列 |
| `main/skpass_cdc.c/.h` | CDC 任务、消息分发、心跳、写卡请求队列、信息查询 |
| `main/reader_frame.c/.h` | `CA 5A` 帧、CRC16、流式解码和坏帧重同步 |
| `main/writer_protocol.c/.h` | 写卡请求校验、固定 Block 1/2 数据布局、结果编码 |
| `main/card_writer.c/.h` | 限定 1K 卡的认证、预读、两阶段写入、回读验证 |
| `main/uf2_update.c/.h` | 一次性更新标志、MSC/FAT16、UF2 校验、OTA 写入 |
| `main/firmware_identity.h` | 格式化完整 32 字节 ELF SHA256，得到 64 个十六进制字符 |
| `components/skpass/` | 由原 CardIO 适配模块延续的 HID 报告、节流及 CardID 辅助接口 |
| `tests/` | 主机侧 CRC/HID、Classic 容错、限定写卡模拟测试 |

`main/i2c_scan_task.c/.h` 是保留的独立扫描示例，**不在当前 `main/CMakeLists.txt` 的 SRCS 中，也未由 `app_main()` 启动**。其中 GPIO21/22、内部上拉不是当前 PN7150 的实际配置，请勿据此接线，或直接与现有 I2C0 总线同时启动。

当前运行流程没有 `KEY_task()`，也没有 `HalLedBlink()` 调用。此前其它工程的按键/LED 调试讨论不能当成本固件功能说明。

## 3. 硬件、接线与供电

### 3.1 目标板

目标模块是 **ESP32-S3-WROOM-1-N16R8**：16 MB Flash、8 MB PSRAM。当前 `sdkconfig` 未启用 `CONFIG_SPIRAM`；应用及 UF2 接收使用内部 RAM，不依赖 PSRAM 才能运行。不能把“模块自带 8 MB PSRAM”误认为“固件已经使用了 PSRAM”。

当前 Flash 配置为 DIO、80 MHz、16 MB。其它板型、Flash 容量或供电方式需重新审核配置，不能仅改型号文字。

### 3.2 接线

| 信号 | ESP32-S3 | 说明 |
| --- | --- | --- |
| PN7150 SDA | GPIO4 | I2C0 数据线，外部上拉 |
| PN7150 SCL | GPIO5 | I2C0 时钟线，400 kHz，外部上拉 |
| PN7150 IRQ | GPIO7 | 高电平表示控制器有数据待读；不是错误指示 |
| PN7150 VEN | GPIO6 | 高电平启用，低电平用于控制器复位/更新时禁用 |
| GND | 共地 | ESP32、PN7150、外部电源必须共地 |
| 原生 USB D−/D+ | S3 USB-OTG（通常 GPIO19/20） | 以开发板原理图与引出端口为准，不要接错到 UART 桥 |
| PN7150 电源 | 当前按外部 5 V 路径配置 | 必须核对模块实际 TVDD/TXLDO 电路 |

`I2C_MASTER_FREQ_HZ=400000`，`glitch_ignore_cnt=7`，SDA/SCL 内部上拉关闭。电阻值、电平转换及走线长度应按模块与总线负载设计，本文不在没有原理图时给出保证适用的电阻值。

**模块输入端接 5 V 不代表 I²C/IRQ/VEN 可以接 5 V。** 应核对模块是否自带稳压、电平转换，确认 ESP32 GPIO 侧不会承受 5 V；不要把裸 PN7150 芯片与带电源电路的模块混为一谈。

### 3.3 两类 USB 端口

- S3 **原生 USB-OTG**：本固件枚举 HID+CDC，更新时枚举 MSC。
- 开发板 **USB-UART 桥**：默认用于 UART0 日志、115200 波特率及适当的首次下载流程。

本工程默认 `ESP_LOGI` 走 UART；CDC 上的诊断是代码显式生成的二进制 `7F` 消息，并非自动重定向所有 ESP 日志。连接正常 CDC 端口用普通串口终端看到二进制是正常现象，不能因此认定乱码/固件损坏。

若板子只有一个端口，先依据原理图确认它实际连到哪里，以及 ROM 下载和应用 USB 枚举的切换方式。

## 4. 编译配置与首次烧录

### 4.1 构建环境

本发行已用 **ESP-IDF 5.2.5** 构建。依赖清单写有 `idf >=5.2`，这是依赖下限，**不等于已经验证 ESP-IDF 6.1 兼容**。

`dependencies.lock` 当前锁定：

- `espressif/esp_tinyusb`：`1.7.6~2`。
- `espressif/tinyusb`：`0.21.0~2`。
- 目标：`esp32s3`；IDF：`5.2.5`。

第一次配置可能需要访问组件仓库下载依赖。`build/` 和 `managed_components/` 是生成目录，没有混入源码发行。Qt 和 AimeIO 的编译器不是本固件的构建依赖。

先激活同一套 ESP-IDF 5.2.5 终端环境，然后从 SKPass 根目录进入 `esp32/` 执行：

```powershell
Set-Location esp32
idf.py --version
idf.py build
```

项目已提供 ESP32-S3 的 `sdkconfig`。确需重新选择目标时再执行：

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

`set-target` 会重新生成目标配置，应先保留需要的自定义配置，再核对 Flash 容量、分区、USB、读密钥及扫描选项。已有 `sdkconfig` 时，修改 `sdkconfig.defaults` 并不保证对应值立即改变。

### 4.2 项目配置

`idf.py menuconfig → PN7150 SKPass`：

| 选项 | 当前默认/配置 | 作用 |
| --- | --- | --- |
| `CONFIG_SKPASS_PLAYER` | `1`，范围 1～2 | USB 序列号 `CARDIOP1` / `CARDIOP2` |
| `CONFIG_SKPASS_SUPPRESS_MS` | `3000`，范围 0～60000 | HID 成功报告与 CDC 读取的独立全局节流间隔 |
| `CONFIG_SKPASS_ENABLE_CDC` | `y` | HID+CDC；关闭后使用纯 HID，不执行 CDC 原始读块流程 |
| `CONFIG_SKPASS_FELICA_FULL_SCAN` | `y` | FeliCa 全量只读探测；需要 CDC，可能明显增加读取时延 |
| `CONFIG_SKPASS_MIFARE_KEY_A` | `574343467632` | Classic 扇区 0 的配置 Key A，12 位十六进制 |

`574343467632` 是社区 WCCFv2 示例密钥，不是所有 Classic 卡的万能密钥。空白卡常用 `FFFFFFFFFFFF`；当前读取流程已有该工厂密钥回退。**固件的读取密钥配置与上位机写卡时选中的当前 Key A，是两个独立来源。**

其它重要配置：TinyUSB HID 数量 1、CDC 数量 1、MSC 缓冲 512 字节、TinyUSB 任务栈 8192、FreeRTOS 100 Hz、UART0 115200、16 MB 自定义分区。代码传入自定义 USB 描述符，所以 `sdkconfig` 中 TinyUSB 的默认 Espressif 描述文字不是实际运行的产品名。

### 4.3 首次部署与旧工程迁移

首次部署需完整写入 bootloader、分区表、OTA 初始数据和应用：

```powershell
idf.py -p COMxx flash
idf.py -p COMxx monitor
```

`COMxx` 必须替换为实际下载/日志端口，不能照抄成 CDC 端口。若串口不可用，先关闭占用它的上位机、游戏或终端，再按开发板方法进入 ROM 下载模式。`monitor` 默认观察 UART 日志，通常用 `Ctrl+]` 退出。

从旧 `pn7150_cardio` 迁移时也必须先完整烧录一次：旧 UF2 更新器按旧项目名校验，可能拒绝 `SKPass` 镜像，**重命名 BIN 文件不能改变镜像内的项目名**。而且应用 BIN 本身不能更换设备上的旧分区表。

烧录改变对应 Flash 内容，请确认板型、串口及目标设备；无需为普通更新执行整片擦除。发行包的首次烧录辅助文件和完整 esptool 命令见 [发行 README](../releases/v1.0/README.md)。

### 4.4 产物与导出

应用镜像：`esp32/build/SKPass.bin`。当前发行镜像：[`../releases/v1.0/firmware/SKPass.bin`](../releases/v1.0/firmware/SKPass.bin)。`first-flash/` 内是配套首次部署辅助镜像，不应拿来做普通 UF2 应用更新。

从 **SKPass 根目录**执行：

```powershell
./scripts/export-firmware.ps1
```

脚本把构建出的应用和首次烧录辅助镜像复制到发行目录。若构建目录不在默认 `esp32/build`，传 `-BuildDir` 指定实际路径。重新导出后，应重新生成/核对发行哈希，不能把旧 `SHA256SUMS.txt` 当成新产物的校验记录。

### 4.5 曾遇到的 CMake/组件管理器错误

历史错误：

```text
--interface_version: invalid choice: 5 (choose from 0, 1, 2, 3, 4)
```

这表示 IDF CMake 发出的组件管理接口版本与被调用的 Python `idf-component-manager` 不匹配，属于**构建环境问题**，不是 PN7150 读卡代码错误。

排查时在同一个终端核对：

```powershell
$env:IDF_PATH
Get-Command idf.py
Get-Command python
idf.py --version
python -m pip show idf-component-manager
```

VS Code 选中的 IDF、Python 环境和当前终端必须一致。先使用本项目已验证的 5.2.5 套件；如果要升级 IDF，需要同步安装匹配的工具与 Python 依赖，并单独做回归。切换 IDF 后不要复用指向旧套件的 CMake 缓存，可以选择一个新的构建目录，例如 `idf.py -B build-idf525 build`。不要靠随意升级组件管理器、修改全局 Python 或删除整个工作区来掩盖混用问题。

另一个历史错误“source directory does not contain CMakeLists.txt”表示选错工程根目录。本固件根目录是 `SKPass/esp32/`，不是 `PN5180-cardio-master` 或 SKPass 顶层目录。

## 5. 启动、任务与恢复状态机

### 5.1 启动顺序

`app_main()` 的实际顺序：

1. `Pn7150_Init()` 配置 VEN/IRQ；VEN 先置高。
2. `Uf2_Update_Boot()` 检查是否为主机请求的更新启动。
3. 如果是更新模式，VEN 拉低，禁用 PN7150。
4. `SKPass_USB_Init()` 按模式安装正常 USB 或 MSC-only 描述符。
5. 正常模式创建 `IIC_task`；更新模式不创建 NFC 任务。

USB 初始化失败时保留 UART 错误日志，并尝试运行 NFC 诊断流程；这不代表 USB 会自动恢复成功。

### 5.2 任务关系

| 任务 | 栈/优先级 | 工作 |
| --- | --- | --- |
| `IIC_task` | 6144 / 3，固定 core 1 | 唯一执行 PN7150/NCI 读写的任务 |
| `skpass_usb` | 3072 / 4 | 消费最新卡片队列，等待 HID 枚举和发送就绪 |
| `skpass_cdc` | 6144 / 4 | 接收主机命令、发送回复/心跳/诊断/读卡事件 |
| TinyUSB 默认任务 | 配置栈 8192 / 优先级 5，当前 affinity core 1 | USB 协议栈 |
| `uf2_enter` | 2048 / 4，临时任务 | 等待约 500 ms 发送确认，再请求软件重启到 MSC |

CDC 回调不直接向 PN7150 发读写命令：写请求进入队列，由 NFC 任务取出；这样避免多个任务争抢 I²C/NCI 及 Crypto1 会话。

### 5.3 正常读取和恢复

```text
I²C 初始化 → 等待 500 ms → VEN 复位 → ACK → NCI 初始化 → Discovery
                                                          ↓
                    无卡超时：继续等待 ← 等待激活/必要时 Select
                                                          ↓
                                           CARD_ID 日志 + HID 排队
                                                          ↓
                                  待写请求优先；否则按节流读取 CDC 数据
                                                          ↓
                                  在场检测（仅适用协议）/重启 Discovery
                                                          ↓
                                                 等待下一次激活

不可恢复的会话/读写错误 → 丢弃 RF Session → 等待 2000 ms → VEN 复位并初始化
```

普通“附近没有卡”的 `ESP_ERR_TIMEOUT` 不触发 PN7150 硬件复位；发现/选择目标后激活失败则是会话异常，不能伪装成普通无卡。

恢复日志中的 `NFC_RECOVERY: hardware reset; initializing PN7150` 是**PN7150 VEN 复位**：拉低 20 ms，拉高后等 100 ms，再清空 RF credits 和最大 payload。它不调用 `esp_restart()`，不会因此重启 ESP32、重新枚举 USB 或重置 CDC 心跳。

相反，进入 UF2 更新模式会主动 `esp_restart()`，用于切换 USB 身份；两者应区分。

`main.c` 的通用日志“PN7150 初始化失败，2000 ms 后重试”也用于初始化成功后的运行期恢复。因此不能只看到这句话就断言 CORE_INIT 出错，应看前一条 `NFC_RECOVERY` 和命令错误。

## 6. PN7150 初始化与 I²C/NCI 收发

### 6.1 地址与 IRQ

PN7150 当前 7 位 I²C 地址为 `0x28`。总线上包含 R/W 位的写地址字节是 `0x50`、读地址字节是 `0x51`。ESP-IDF 的 `device_address` 和 `i2c_master_probe()` 必须传 **0x28**，不能传 0x50/0x51。

IRQ 高表示 PN7150 有数据需要读取，不表示故障。此前 `KEY_task()` 把正常 IRQ 日志记成 `E` 容易误导；当前驱动不把 IRQ 正常高电平当错误打印。

IRQ GPIO 配置了上升沿类型，但当前源码没有安装 GPIO ISR/通知任务，实际接收使用 `gpio_get_level()` 轮询等待。不能据配置中的 `GPIO_INTR_POSEDGE` 宣称已实现中断驱动接收。

### 6.2 当前初始化命令

下列字节是 **完整 NCI 控制帧**，不是 CDC 帧，也不是要直接发给卡的 RF 命令：

| 步骤 | 命令 | 成功响应检查 |
| --- | --- | --- |
| CORE_RESET（保留配置） | `20 00 01 00` | 按 NCI LEN 收帧，检查 6 字节响应，前缀 `40 00 03 00` |
| CORE_INIT | `20 01 00` | `40 01`，状态 `00`；按 NCI LEN 收 payload |
| 厂商激活 | `2F 02 00` | `4F 02`，状态 `00` |
| 读回 TVDD | `20 03 03 01 A0 0E` | `40 03`，状态 `00`，核对扩展参数 ID、长度和值 |
| 外部 5 V 配置（仅需更新时） | `20 02 07 01 A0 0E 03 06 64 00` | `40 02`，状态 `00` |
| TOTAL_DURATION | `20 02 05 01 00 02 00 01` | `40 02`，状态 `00`；小端 `00 01` = 256 ms |
| RF_DISCOVER_MAP | `21 00 10 05 01 01 01 02 01 01 03 01 01 04 01 02 80 01 80` | `41 00`，状态 `00` |
| RF_DISCOVER | `21 03 09 04 00 01 01 01 02 01 06 01` | `41 03`，状态 `00` |

供电配置中：`07` 是 payload 长度，`01` 是参数数量，`A0 0E` 是扩展参数 ID，`03` 是该参数数据长度，`06 64 00` 是本工程采用的外部 5 V 配置。它与 ElectronicCats 的 PN7150（2ndGen）外部 5 V/TXLDO 分支一致；**不要拿 PN7160 的 3rdGen 长度/参数直接替换**。

用户此前明确指出 `A0 0D 03 78 08 00` 不是来自本天线的测试结果。当前没有发送这个参数，保留控制器已有的 RF/天线配置，仅按既定电源路径配置 `A0 0E`。这不是“已经给天线调好参数”，也不保证任意模块的 5 V 输入就是该电源路径。

`CORE_RESET` 使用 ResetType `00` 保留配置，避免每次恢复都重置后再写回 TVDD。GET 成功且 `A0 0E` 的 3 字节值为 `06 64 00` 时跳过 SET；值不同或 GET 返回不支持等非零状态时尝试原配置 SET。传输超时或格式错误会中止初始化，避免用不完整数据决定写入。`TOTAL_DURATION` 每次初始化正常设置。

`CORE_INIT_RSP` 的接口列表是变长的，版本位置为 `17 + response[8]`，再往后两字节依次是 `FW_MAJOR_NO`、`FW_MINOR_NO`。驱动检查三个字段都在完整帧内才打印，避免短帧越界。例如此前的 `40 01 19 ... 10 01 A0` 对应 `ROM_CODE_V=10`、`FW_MAJOR_NO=01`、`FW_MINOR_NO=A0`；这是 PN7150 芯片版本，与 ESP32 应用版本不同。复位/初始化期间的 `60 07 01 E6` 按 Anti-tearing 通知记录并消费，继续等对应 RSP，随后读回核对 TVDD。

映射含 T1T/T2T/T3T→Frame、ISO-DEP→ISO-DEP、MIFARE→厂商 Tag-command。Discovery 轮询 NFC-A、NFC-B、NFC-F、ISO15693，频率字节都为 `01`；ISO-DEP 可通过 NFC-A 或 NFC-B 发现。映射能够激活某协议，不代表应用已经实现它全部的数据读写。

### 6.3 NCI 报文不能混为一种响应

常见报文：`40/41/4F` 为相应控制响应 RSP，`60/61/6F` 为通知 NTF，`00` 或含分片标记的连接 0 DATA 为卡数据。特别要分清：

- `41 06 ...`：RF_DEACTIVATE_RSP。
- `61 06 ...`：RF_DEACTIVATE_NTF，描述实际去激活类型/原因。
- `61 05 ...`：RF_INTF_ACTIVATED_NTF，才是激活信息。
- `60 06 ...`：CORE_CONN_CREDITS_NTF，数据流控，不是卡内容。
- `60 07 ...`：CORE_GENERIC_ERROR_NTF，不是 CARD_ID。

初始化响应成功不等于有卡激活；发现阶段只输出 `RF_DISCOVER_CMD 响应校验通过` 后安静等待卡是正常状态。

### 6.4 I²C 读写实现

全部控制器报文（包括 CORE_RESET/CORE_INIT）先读 3 字节 NCI 头，再依据第 3 字节 LEN 读 payload；缓冲容量最多可接收 3+255 字节。

设备句柄按总线缓存：首次收发调用 `i2c_master_bus_add_device()`，随后 transmit/receive 共用该句柄；PN7150 硬件复位不卸载设备。只有调用者切换总线时，才移除旧设备并挂载新设备。调用者必须保持旧总线在设备挂载期间有效；现有工程由单个 NFC 任务串行收发，生命周期与应用一致。单次 I²C 调用超时 100 ms；控制命令匹配响应的等待窗口约 1000 ms。总耗时还包含分段 I²C 调用和调度，不是硬实时保证。

GPIO7 上升沿 ISR 只给二值信号量并唤醒任务，不做 I²C 或日志。`Pn7150_Wait_Irq()` 先检查电平，高则立即收包；低则阻塞等待，醒来及超时后再检查。旧信号但电平仍低会继续等待原截止时间。发送前和硬件复位时清旧信号；不能在发送后清，以免清掉快速响应的边沿。IRQ 连续保持高时无需新的边沿也能读下一包。没有 1 Tick GPIO 轮询，也没有无卡超时后的额外 10 ms 延时。共享 GPIO ISR 服务已安装时复用服务，重复初始化不重复挂回调。

`Pn7150_Transceive()` 仅把与命令匹配的响应头（命令首字节 +0x20、同 OID）交给调用方；等待期间消费 credit 通知，并跳过迟到 DATA 等其它包，避免把它们误判为 RF_DEACTIVATE 响应。这是本工程对参考驱动流程的重要防御处理，但不是通用的全部 NCI 消息调度器。

### 6.5 卡 DATA 流控

`Pn7150_Tag_Transceive()` 用静态 RF connection 0，把卡命令装成 `00 00 LEN payload`。激活通知提供初始 credits 和最大 payload；没有 credit 时先等 credit 通知，发送成功后减 credit。`0xFF` 表示关闭流控的特殊值，不被普通累加覆盖；普通累计限制在 254。

接收时消费 `60 06`，将 DATA 分片拼接，直到 PBF 标志清零才返回。若收到 RF 去激活，返回 `ESP_ERR_INVALID_STATE`；通用错误返回无效响应；总等待窗口约 1500 ms。接口拒绝超过 255 字节或超过当前最大 payload 的单次请求，**没有实现发送端任意大消息分片**。

NCI DATA 没有每次 READ 的事务编号。超时后直接发下一块，可能把迟到的上一块数据当成新块数据；Classic 的恢复因此采用 Sleep/Select 同步，而不是无条件重发 DATA。

## 7. 卡片激活、UID 与协议判别

### 7.1 激活通知处理

`Pn7150_Read_Activated_Notification()` 并非“IRQ 高就把下一包当 UID”：

- 收到 `RF_DISCOVER_NTF` 时记录目标的实际 discovery ID、协议与所需接口；通知收束后发 `RF_DISCOVER_SELECT_CMD`。
- ISO-DEP 选择接口 `02`，MIFARE 选择接口 `80`，一般标签使用 Frame `01`；不把所有卡都映射到 Frame。
- 收到 `RF_INTF_ACTIVATED_NTF` 才解析卡信息，并更新 RF credits/max payload。
- 忽略已排队的去激活通知；通用错误 `A1` 按目标激活失败继续轮询，其他通用错误记录警告。
- 有目标/已 Select 后仍等不到激活，返回无效状态，进入恢复；单纯无卡超时则正常继续等。
- 激活函数有约 2000 ms 的整体等待门限及单次接收等待；不无限等待已经失效的选择流程。

发现通知类型 `02` 表示还有候选，`00/01` 为本轮结束。驱动比较本轮所有候选，包括最后一条，按 **MIFARE `80` / T3T `03` > T2T `02` > ISO15693 `06` > ISO-DEP `04` > 其他** 选择实际 discovery ID、协议和接口；同优先级保留先收到的候选。不做多卡公平调度，写卡和数据调试均建议一次只放一张卡。

### 7.2 `pn7150_card_info_t`

结构包含 discovery ID、RF interface、protocol、mode technology、最多 10 字节 UID 及其长度、SAK/有效标志、PMm/有效标志、system code/有效标志。

| 激活 mode | 解析内容 | 处理要点 |
| --- | --- | --- |
| `00` NFC-A | NFCID1/UID，可能有 SAK | 校验 UID 长度和参数边界，支持最长 10 字节 |
| `01` NFC-B | 4 字节标识/PUPI | 按 SENSB_RES 布局提取；不是 access code |
| `02` NFC-F | 8 字节 IDm，可能有 PMm/system code | 不因 IDm 首字节为 01 就擅自右移；仅明确带响应码的长度布局才跳过响应码 |
| `06` ISO15693 | 8 字节 UID | 转成当前项目统一的字节顺序 |
| 其它 | 尚未实现对应标识提取 | UID 长度可能为 0，输出警告 |

增加 `pn7150_card_info_t` 的字段本身不会在 RF 上改变卡片行为。此前“修改结构后读卡失败”需要结合同时发生的报文匹配、RF 状态与协议处理变化分析，不能仅归因于结构体名称。

### 7.3 几种号码必须区分

- **UID/NFCID1**：NFC-A 防冲突/选择阶段得到的标识。
- **IDm**：FeliCa 的 8 字节标识；当前代码复用 `uid` 字段承载它。
- **PMm**：FeliCa 的 8 字节参数，不是第二个卡号。
- **system code**：FeliCa 系统信息，可能未包含在当前激活数据中。
- **Block 数据/S_PAD0**：读卡交易获得的卡内原始内容，需要进一步解析。
- **access code/卡背号码**：通常指 PC 端解析或查询获得的 20 位十进制号码；不与上述标识直接画等号。

UART 上的 `CARD_ID` 指 UID/IDm 等 RF 标识，不承诺是卡背号码。CDC 基础读卡事件 ID 槽只有 8 字节，10 字节 UID 的完整内容由 `TAG uid=...` 诊断提供；写卡绑定使用完整物理 UID，不使用 HID 的兼容掩码值。

## 8. Classic 读取、认证与有界重试

### 8.1 卡型和读取范围

`mode=00` 且 `protocol=80`，或 SAK 为 `08/18/09` 时进入 Classic 候选分支。实际读块要求 `rf_interface=80`；接口不对返回事件状态 `15`，不试图在错误接口上发送认证命令。

读取范围固定为 **扇区 0 的 Block 0、1、2，共 48 字节**。Block 3 是扇区尾块，当前不读取它；其它扇区没有实现全卡遍历。能识别 Classic 候选不等于已完整支持所有 Classic 容量和变体。

### 8.2 免认证试读与 Key A

先尝试逐块免认证读取 Block 0～2。某些卡可直接返回数据；只有三块都完整通过才上报 48 字节。正常 Classic 对未认证 READ 的短 NACK 不反复补试，直接转入认证路径。

认证读取按以下顺序：

1. 检查配置 Key A 是合法的 12 位十六进制，转成 6 字节。
2. 重选当前卡，尝试配置 Key A。
3. 若结果是认证失败 `12`，且配置密钥不等于 FF，尝试 `FFFFFFFFFFFF`。
4. 若是会话/读块失败 `13`，不随意切换密钥；若重选失败，不继续用旧会话认证。
5. 完整三块成功才设置 `mifare_len=48`；最终失败则清空部分数据，不把一两块数据误称完整卡号来源。

PN7150 Tag-command 的认证 payload：`40 00 10 key[6]`，其中 `00` 为扇区 0；不是直接发送 raw RF 的 `60/61 + UID + key`。认证成功要求回应**恰好两字节 `40 00`**。

READ payload：`10 30 block`。成功要求接收 18 字节：`10 + 16 字节块数据 + 00`。外层 `ESP_OK` 只说明 Tag transceive 收到了可返回的 DATA，不替代这一内容校验。

### 8.3 为什么加帧间隔

用户在弱耦合/加壳情况下出现“UID 能读、认证或中途 Block 2 失败”，因此读取模块增加了帧间隔，用来降低过密连续命令可能带来的偶发时序问题：每次 READ 前、重选前和认证前调用 `frame_gap()`。

代码常量为 `MIFARE_GAP_MS=5`，但不是直接 `pdMS_TO_TICKS(5)`：

```c
vTaskDelay((MIFARE_GAP_MS + portTICK_PERIOD_MS - 1) /
           portTICK_PERIOD_MS + 1);
```

当前 100 Hz 下一个 tick 是 10 ms，该公式请求 2 ticks，通常实际等待约 **10～20 ms**；若任务被延后，可能更长。直接 `pdMS_TO_TICKS(5)` 在当前配置下可能为 0，不能实现需要的间隔。没有使用几毫秒忙等待来长期阻塞 USB 任务。

### 8.4 软重试的实际含义

`MIFARE_ATTEMPTS=3`：初次 + 最多 2 次补试。不是每次错误都硬件复位，也不是无限重试。

- 每种密钥的初始认证最多 3 次。
- 认证后每个块最多 3 次尝试；补试认证失败也算一次机会，避免嵌套重试倍增等待。
- 补试只重读失败块，先前取得的完整块保留在暂存区；最后任何一块失败则整体清空。
- 每次补试之前重选并重新认证同一密钥，重建 Crypto1/RF 会话。
- 重选失败立即终止软重试；不能在错误 UID、错误接口或失步会话下继续。

重选序列为：

```text
21 06 01 01             RF_DEACTIVATE(Sleep)
等待 RSP 成功与 Sleep NTF
21 04 03 <discovery_id> <protocol> <interface>
等待 RSP 成功与 RF_INTF_ACTIVATED_NTF
核对同 UID 长度/内容、同协议、同接口
重新认证 → 重新 READ 失败块
```

这属于**不拉 VEN、不复位 ESP32 的软件恢复**，但并非强行保持已经失败的认证会话。它还用于避免超时后的迟到 DATA 被错配给后续读块；此同步设计需要实机验证，主机 mock 不证明真实 Crypto1 恢复一定成功。

### 8.5 日志判读

```text
MIFARE block=00 result=ESP_OK raw=100400 attempt=1/1 start_ms=...
MIFARE AUTH key_slot=configured result=ESP_OK size=2 status=03 attempt=1/3 ...
MIFARE AUTH key_slot=factory_FF result=ESP_OK size=2 status=00 attempt=1/3 ...
```

第一行是可能出现的免认证试读：`attempt=1/1` **不表示整份固件只有一次重试**。`100400` 或 `10B0` 不满足完整 18 字节数据布局，不能作块数据。认证 status03 是失败，不证明“空白卡”，也不能单独排除 RF/会话问题而认定一定是错 Key。

若出现：

```text
MIFARE RESELECT result=ESP_ERR_TIMEOUT; soft retry aborted
NFC_RECOVERY: MIFARE read failed; discarding RF session
```

说明已经在认证前的 Sleep/Select 同步处失败，补试被安全中止，然后主循环走 PN7150 恢复。版本已更新也不代表这种情况必然消失；严重耦合不足会使重新选择本身失败。

偶发成功恢复可见 `MIFARE RETRY ... action=reselect_reauth` 和 `MIFARE RETRY recovered; continuing sector read`。比较间隔应看 `start_ms`，不是 PC 收到日志的时间；后者还含 USB/串口排队延迟。

## 9. FeliCa 读取与 access code 的边界

### 9.1 只读命令

NFC-F、UID 长度 8 时进入 FeliCa 分支，复制 IDm/PMm，发送 **Read Without Encryption（06）**。单块请求 payload 为 16 字节：

```text
10 06 <IDm 8字节> 01 <service 2字节> 01 80 <block>
```

此处首字节 `10` 是十六进制长度 16，不是 Classic Tag-command 的命令标记。不要把两种 payload 混用。

不写 RC/CK、不假设认证秘密、不调用 FeliCa Authentication、不尝试绕过卡片访问控制。

### 9.2 服务和块探测

基础读取先探测系统块 `82`，再探测用户块 `00`；每块依次发送四组线上服务字节：`0B00`、`000B`、`0900`、`0009`。

服务号在线上按 FeliCa 规定的字节序编码；这些日志表示**实际发送的两字节序列**，其中包含正反字节序的探测，不应写成“四个已确认的标准服务”。系统块 82 的原始数据供 PC 侧 DFC/发行方分析；它不是直接作为 SPAD 事件载荷发送。

第一个成功读出的块 00 内容作为 `spad[16]`；其它结果保留在诊断日志。`FULL_SCAN=y` 时继续只读扫描用户块 `00..0D` 和系统块 `80..88`，跳过已经探测过的 00/82。理论共 23 块 ×4=92 条探测；遇到传输超时或会话失效会提前终止，未执行的项不能统计成卡片拒绝。

如果已确认自己的卡在基础路径就能取得需要的数据，可关闭 `SKPASS_FELICA_FULL_SCAN`，减少额外探测；仍保留 82/00 的基础探测。这是配置选择，不代表本文已修改默认值。

### 9.3 回应验证

必须同时校验：回应码 `07`、相同 IDm、FeliCa 长度字段、块数 1、Status1/Status2 都为 0，以及 PN7150 Frame 接口附加尾状态为 0（若该布局包含尾状态）。单块成功 FeliCa 长度应为 29；之后才提取 16 字节。

诊断中 `status1=FF status2=FF` 是没有解析出有效卡状态的占位，可能由超时、RF 错误或损坏响应造成；不是自动等同卡返回 FF/FF。没有取得 S_PAD0 不能直接断言“这种卡必须认证”或“所有该品牌卡都不可读”。

### 9.4 Classic AiMe、Banapass、Amusement IC 与发行方

此前讨论的 `0 / 3 / 5` 是 access code 类型/前缀体系的背景信息，**不是 NCI protocol、CDC card_type 或硬件接口编号**。同样，SEGA、Bandai Namco、Konami、Taito 是发行体系，不能仅靠“FeliCa/Classic”或 UID 首字节可靠判定。

固件只提供 RF 标识、PMm、可读原始块及状态；发行方/DFC/20 位号码解析由 PC 端完成。读取 S_PAD0 成功仅说明取得了候选原始数据，固件日志明确写 `decoding and validation required`，不宣称已核对印号。

此前上位机能显示卡背 access code，而游戏显示不同的 20 位号码，涉及电脑端采用的号码来源。当前 Qt 与 AimeIO 都尝试本地解析 S_PAD0；成功时 AimeIO 用解析后的 10 字节 BCD 通过 AiMe ID 接口呈卡，失败时回退 FeliCa IDm。Qt 的 IDm 派生号不是卡背印号，也不是 DLL 直接传给游戏的号码。完整规则见 [卡型与 access code](access-codes.md)。这一呈卡方式不会改写实体卡的 RF 类型或卡内数据。

## 10. 移卡、下一张卡与同卡重复刷入

### 10.1 在场检测仅针对 ISO-DEP

当前 `Pn7150_Wait_Card_Removed()` 只支持 `protocol=04` 且 `rf_interface=02`：每约 500 ms 发厂商在场检测 `2F 11 00`，校验 `4F 11 01 00` 响应，再等待 `6F 11 01 xx` 通知。

- `xx=01`：仍在场，继续等。
- `xx=00`：离场，才产生 UART `CARD_REMOVED` 和 CDC `11`。
- 命令/通知布局不匹配或超时：返回失败，进入恢复，不伪造“卡已移走”。

Classic、FeliCa 等非 ISO-DEP 协议返回 `ESP_ERR_NOT_SUPPORTED`，主任务直接准备下一次 Discovery，**不会每隔 500 ms 假装发 CARD_REMOVED**。所以 PC 端不能假定所有卡都会提供可靠的物理离场事件。

### 10.2 重新 Discovery

正常结束当前连接使用 `21 06 01 03`，即去激活到 Discovery；等待匹配 RSP 后，还必须等待并确认 `61 06` 通知的 type 为 `03`，才能认为重新轮询已完成。正常成功路径不多发一次可能不合时宜的 RF_DISCOVER。

如果 RSP 状态 `06`，当前代码转为显式请求 `RF_DISCOVER`，而不是仅凭失败响应假定已经在 Discovery。Start_Discovery 接受状态 `00` 或 `A0`（已开始），否则恢复 PN7150。

**状态命名说明：**NXP 的 NCI 定义中 `06` 是 `SEMANTIC_ERROR`，`REJECTED` 实际为 `01`。当前源码注释/日志 `RF_DEACTIVATE rejected` 是该错误处理分支的文字标签，不是准确的标准状态名。历史文档沿用该文字时，应以实际字节和 NCI 定义判断。

### 10.3 同一张卡能再次刷，不需另一张卡解锁

每次激活都提交给 HID，代码没有永久记住 last UID 并拒绝它。HID 以成功发送时间做全局节流；CDC 以允许读取的时间做另一份全局节流。默认都是 3000 ms，但状态相互独立，也**不是只对同 UID 生效**：刚刷 A 后立即刷 B 也可能被节流。

非 ISO-DEP 卡放在场中可能被反复激活和读取；其刷入周期还受扫描、认证、重选及恢复耗时影响，不承诺精确每 3 秒一次。移走后再次放入同卡不需要先刷不同卡，但过快再次刷可能被时间抑制。

ISO-DEP 由于保持在场检测，会等离卡后重启发现。这与非 ISO-DEP 卡的立即重新 Discovery 路径不同。

上位机中的 10 秒预览超时、相同完成结果避免重复刷新、本地解析缓存是电脑端策略，当前没有联网查询。成功/失败真正变化时仍会更新结果；不能通过相同结果不刷新推断底层 RF 已经稳定。同卡缓存也不能让物理卡在一次会话失败后自动完成扇区读取。

## 11. CardIO 兼容 HID

### 11.1 USB 描述符

正常模式 VID=`1CCF`、PID=`5252`，bcdDevice=`0100`，厂商/产品名均为 `SKPass`。玩家序列号保留 `CARDIOP1/CARDIOP2` 以兼容既有客户端，并非按每块 ESP32 唯一芯片 ID 生成。

| 模式 | 接口 | 端点 |
| --- | --- | --- |
| HID+CDC 正常 | HID 0；CDC control 1、data 2；IAD 复合设备 | HID IN `81`；CDC notify `82`；CDC OUT `03`/IN `83` |
| 纯 HID | HID 0，设备类 0 | HID IN `81` |
| UF2 更新 | MSC 0，PID 改为 `5253` | MSC OUT `01`/IN `81` |

HID 端点 max packet 16 字节，轮询间隔 1；报告仍为 **9 字节**，不是 16 字节报告。usage page 保留 `FFCA`，这是兼容协议，不是普通键盘 HID。

### 11.2 报告内容

```text
byte 0    Report ID：01=ISO15693；02=FeliCa 或 ISO14443
byte 1..8 兼容卡标识，8字节
```

- FeliCa：直接使用 IDm8。
- ISO15693：驱动已统一 UID 字节序，USB 发送层不再重复反转。
- NFC-A/B：复制 UID 的前最多 8 字节，不足补 0，再将首字节 `&=0x0F`，保留原 CardIO 兼容规则。
- 10 字节 NFC-A UID 在 HID 中会截断，不能用 HID 标识做精确写卡绑定。

`tud_hid_report(report[0], &report[1], 8)` 由 TinyUSB 添加 Report ID，不能再把完整 9 字节当 payload，否则会产生重复 ID。GET_REPORT 没有实现通用数据查询，SET_REPORT 不执行写卡命令。

### 11.3 队列与发送节流

HID 待发送队列长度为 1，保留最新激活。USB 尚未 mounted/ready 时任务每约 100 ms 再试，期间新卡可覆盖旧待发送值；NFC 任务不因 PC 尚未枚举而一直阻塞。

`SKPass_Send()` 仅在 sender 成功后记录上次发送时间，默认 3000 ms 全局抑制。由于这是最新值队列，**不保证离线期间的每一张卡都按顺序补报**，也不是“卡始终在场”的状态协议。

保留 VID/PID、序列号、Report ID 和字段并不是忘记改名，而是为了原 CardIO 客户端、AimeIO/测试工具仍能识别；自有产品显示/API 名称已改为 SKPass。

## 12. CDC 串口协议与队列

### 12.1 帧格式

实现位于 `reader_frame.c/.h`。所有二进制消息使用同一协议版本 1：

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 2 | 固定帧头 `CA 5A` |
| 2 | 1 | 协议版本 `01` |
| 3 | 1 | 消息 ID |
| 4 | 2 | payload 长度，LE16 |
| 6 | N | payload，最多 512 字节 |
| 6+N | 2 | CRC16，LE16 |

总长度为 `N+8`，最大 520。CRC 为非反射 CRC16/CCITT-FALSE 参数：poly `1021`、init `FFFF`、xorout `0000`；计算范围从版本字节开始，即 **version + msg + length + payload**，不包含 `CA 5A` 和 CRC 本身。

串口是字节流，一次 read 不保证恰好一帧。解码器累积分包、连续处理粘包；坏 CRC、错误版本/长度、启动文本或其它噪声通过逐字节移动寻找下一帧头重同步。CRC 用于发现传输损坏，不是身份鉴权或内容加密。

### 12.2 消息 ID

下面消息 ID 均为**十六进制**：

| 方向 | ID | payload | 用途 |
| --- | --- | --- | --- |
| 设备→PC | `01` | UTF-8 固件身份字符串 | 每约 1 秒心跳，确认协议及能力 |
| 设备→PC | `10` | 原始卡片事件 | 卡类型、状态、ID/PMm、SPAD/Classic 数据 |
| 设备→PC | `11` | 空 | 已确认 ISO-DEP 离场 |
| 设备→PC | `7F` | UTF-8 诊断文本 | TAG、MIFARE、FELICA、NFC_RECOVERY/NFC_READY 等 |
| PC→设备 | `20` | 固定 31 字节 | 限定 Classic 1K 写卡请求 |
| 设备→PC | `90` | 固定 51 字节 | 写卡接收/最终结果 |
| PC→设备 | `30` | 空 | 请求重启进入 UF2 MSC |
| 设备→PC | `B0` | 1 字节状态 | `00`已安排进入；`01`写卡忙；`02`进入失败 |
| PC→设备 | `31` | 空 | 获取设备信息，不进入更新模式 |
| 设备→PC | `B1` | UTF-8 JSON | 本次查询的固件信息和 NFC 状态快照 |

当前不自动发送详细信息 `02`，也不提供任意主机 NFC 命令。未知消息被忽略；参数不合法的固定写卡请求会返回拒绝结果。

### 12.3 卡片事件 `10`

使用 `Aime_Encode_Event()` 逐字段编码，不能直接把 C struct 内存 memcpy 到 USB，避免 padding/ABI 差异。

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 1 | card_type |
| 1 | 1 | status |
| 2 | 8 | IDm/基础标识槽 |
| 10 | 8 | PMm；未取得时全 0 |
| 18 | 1 | SPAD 长度 S |
| 19 | S | SPAD 数据 |
| 19+S | 1 | Classic 数据长度 M |
| 20+S | M | Classic 数据 |

总 payload 长度为 `20+S+M`。当前常见无块数据 20 字节、FeliCa SPAD16 为 36 字节、Classic48 为 68 字节；缓冲容纳的最大组合为 84 字节。接收方必须按 S/M 实际值解析并校验边界，不能假定固定 struct 长度。

card_type（十六进制）：`10` Classic、`20` FeliCa、`30` ISO15693；其它未支持路径可能为 `00`。这与 HID Report ID 及 access code 发行类型完全不同。

| event status（十六进制） | 含义 |
| --- | --- |
| `00` | 当前读取路径成功；ISO15693 是标识事件，不表示读了扇区 |
| `11` | 未取得 S_PAD0，需结合传输/服务/卡状态判读 |
| `12` | Classic 认证失败或配置 Key A 格式不合法 |
| `13` | Classic 读块失败/重选失败/会话失效 |
| `14` | FeliCa PMm 不可用；仍可能有 SPAD 数据 |
| `15` | Classic RF interface 不匹配 |
| `16` | 当前应用尚未支持该卡的数据读取 |

失败时保留已知基础标识，但 Classic 部分块内容不会被冒充完整 48 字节上报。完整块数据都是 0 也需要由 PC 按布局判断是否空白；认证失败则更不能直接判断空白。

### 12.4 心跳、连接与消息队列

心跳 payload 当前为 `SKPass-2.5.0-retry-uf2-write`。CDC 连接首次建立还会发送一行裸文本 `<firmware> ready\r\n`；二进制解析器应跳过它，不能仅凭这行文字确认协议。CDC 操作以 DTR/`tud_cdc_connected()` 为连接条件，普通串口工具可能需要打开 DTR。

队列：读卡事件长度 1（最新值覆盖）；诊断长度 96；回复长度 16；待写请求长度 1。发送优先级为 **回复 → 到期心跳 → 诊断/离场 → 原始卡事件**，帧分多次 USB write 时用 offset 继续。大量诊断可能延后卡事件；诊断队列满会丢弃并计数，因此完整 RF 排障优先保留 UART 记录。

`SKPass_CDC_Should_Read()` 的节流在**开始一次读取尝试**时更新，失败也计入间隔；它不是只针对成功结果，也不按 UID 区分。只要 CDC 模块/队列已初始化，即使当前 PC 没打开串口，固件仍可能执行读取；断线不是停用 NFC 的指令。

USB 心跳来自独立 CDC 任务，所以 PN7150 会话失败、正在重试或正在 VEN 恢复时仍能发心跳。PC 应分别观察：

- 心跳：USB/CDC 通道是否活着。
- `NFC_READY`/`NFC_RECOVERY`：RF 初始化和恢复过程。
- `10` 的状态与数据：本次卡片读取是否成功。

### 12.5 与游戏/上位机的关系

Windows 串口通常独占。游戏中的 AimeIO DLL 打开 CDC 后，上位机可能无法打开同一 COM；关游戏后上位机正常并不能说明固件在游戏内的串口路径一定配置正确。使用前检查 DLL、端口选择及占用者，而不是先怀疑 HID。

HID 与 CDC 是不同 USB 接口，可以由不同功能使用，但不自动把 CDC 分成多个客户端广播流。虚拟键盘刷卡由 PC DLL处理，不需要连接读卡器；实体刷卡需要正常 CDC 数据及 PC 端正确解析。

## 13. 限定 Classic 1K 写卡

### 13.1 授权范围与不会做的事

写卡范围来自此前确认：仅对自有、可改写的 **Classic 1K** 写 **Block 1、Block 2**。不要对正在使用、不可接受数据丢失的游戏卡进行试写。

固件要求 `mode=00`、`protocol=80`、`interface=80`、SAK 有效且为 `08`，以及完整 UID 与请求相同。未知 SAK、FeliCa、Ultralight、Classic 4K 不进入写路径。

固定布局：

```text
Block 0：不写，保留 UID/厂家信息
Block 1：00 × 16
Block 2：00 × 6 + 20位数字的10字节BCD
Block 3：不写，保留 Key A、访问控制、Key B
```

这会覆盖 Block 1 的全部内容和 Block 2 的全部内容，不是只改 Block 2 末尾。并不克隆 UID，也不将卡注册到官方或社区服务器。

上位机可接受手动号码，或通过 Classic Serial 算法/随机 Serial 生成号码；随机按钮只生成，保存/写入需要用户操作。生成、完整号码格式校验与 GUI 交互是 PC 侧实现，固件仅验证固定请求的 BCD 等底线条件；不保证随机号全球唯一、账号存在或所有游戏接受。

### 13.2 当前 Key A 的含义

写请求内带的是**目标卡当前使用的 Key A**，不是要设置的新密钥。空白卡一般先尝试 FF；如果当前密钥是其它值，必须使用正确值。

由于不写 Block 3，FF 卡写完仍是 FF 密钥；选 WCCFv2 并不会自动把卡改成 WCCFv2 密钥。即使卡内写入 20 位号码，也不能保证要求不同认证/权限的原装读卡器能使用它。

写卡模块与读取软重试模块独立；**当前没有自动写重试/回滚**，也不能把 Classic 读取的帧间隔和三次重试描述成已应用于全部写卡步骤。

### 13.3 请求 `20`：固定 31 字节

| payload 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 4 | 请求 ID，LE32，非 0 |
| 4 | 1 | UID 长度，仅 4/7/10 |
| 5 | 10 | 完整物理 UID，剩余字节必须补 0 |
| 15 | 6 | 当前 Key A 原始字节 |
| 21 | 10 | 20 位号码 BCD，高 nibble 为前一位，低 nibble 为后一位 |

`writer_decode_request()` 拒绝错长度、ID=0、不合法 UID 长度/补零、非十进制 BCD nibble 和全零号码。不会接受任意块号或任意 payload 地址。

一次只允许一个待写/正在写的请求；最近请求 ID 相同会被拒绝，防止立即重复执行，但这不是跨重启的永久去重数据库。主机也不能把断线/超时当理由自动重发写命令。

请求有效期为入队后 15 秒，只有同 UID 卡激活才取出执行；其它卡不会被误写。执行前的断线/DTR 下降带有 sticky 取消标志，快速重连不能让旧待写请求复活。超时检查由 NFC 任务执行，**实际失效通知可能晚于 15 秒**；过期请求不会在之后才开始写入。

### 13.4 执行流程

1. 严格检查 1K 卡型和完整 UID。
2. `40 00 10 key6` 认证；失败停止。
3. 预读 Block 1/2；任一失败不发写命令。
4. 写 Block 1：`10 A0 01` → 校验 ACK → `10 data16` → 校验 ACK。
5. 写 Block 2：`10 A0 02` → 校验 ACK → `10 data16` → 校验 ACK。
6. 逐块 READ 回读，逐字节比较期望的 32 字节。
7. 所有步骤成功才返回 `WRITE_SUCCESS`；其它结果丢弃 RF 会话并走原有恢复。

写 ACK 可接受 `10 00`，或明确带 RF ACK 的 `10 0A 00`；例如 `10 04 00` 中即便末尾 00，也不能把 RF NACK04 当写入成功。底层返回 `ESP_OK` 仍不足以证明写成功。

MIFARE 两个块不具备事务原子性。写完 Block 1 后卡移走或掉电，可能只改完一块；预读数据不是持久备份，固件不自动回滚。执行已经开始后，CDC 断开也可能继续完成，此时 PC 的结果未知，必须重新读卡确认，不能盲目再次写入。

### 13.5 结果 `90`：固定 51 字节

| payload 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 4 | 请求 ID，LE32 |
| 4 | 1 | status |
| 5 | 1 | stage |
| 6 | 1 | detail |
| 7 | 1 | UID 长度 |
| 8 | 10 | UID |
| 18 | 1 | verified_mask |
| 19 | 32 | 回读 Block 1/2 |

下面 status/stage 为**十进制**，不是 CDC 消息 ID 的十六进制写法：

| status | 含义 |
| --- | --- |
| 1 | 已接收/排队，不表示已经写入 |
| 2 | 请求过期 |
| 3 | 参数、卡型或重复 ID 拒绝 |
| 4 | 忙 |
| 5 | 写入并回读比较成功 |
| 6 | 认证失败 |
| 7 | 预读失败 |
| 8 | 写命令/写数据失败 |
| 9 | 回读/内容比较失败 |
| 10 | 执行前断开取消 |

stage：0 排队/完成；1 认证；2 预读；3/4 写 Block 1 命令/数据；5/6 写 Block 2 命令/数据；7/8 回读 Block 1/2。

detail 保留控制器/卡状态；`FF` 也可能表示没有有效回应，并非一定是卡返回 FF。`verified_mask` bit0/bit1 表示相应块已经读回有效数据，**不单独代表内容匹配**。accepted/失败结果中的块数组可能不完整，不能据此推断卡内容或成功。

写请求中的 Key A 通过本地 USB 明文传输；CRC 不防恶意本机程序。不要公开原始写卡 USB 抓包、密钥或完整 access code；协议没有远程认证/加密能力。

## 14. 按需获取设备信息

此功能按用户要求：在上位机“固件更新”页点击 **获取设备信息** 才查询，不在顶部常驻、不连续上报版本详情。

主机发 `31` 空载荷，固件发 `B1` UTF-8 JSON：

```json
{
  "schema": 1,
  "firmware": "SKPass-2.5.0-retry-uf2-write",
  "version": "2.5.0",
  "built": "Oct 11 2026 17:02:45",
  "idf": "v5.2.5",
  "target": "esp32s3",
  "app_sha256": "<64个十六进制字符>",
  "usb_state": "connected",
  "nfc_state": "ready"
}
```

构建时间为当前发行示例，自己重新编译后会变化。版本取自 ESP-IDF app description（本工程 `version.txt`）；SHA 是 app description 内的 **ELF SHA256**，不等于 BIN 文件整体 SHA256。格式化直接读取完整 32 字节摘要，并使用 volatile 访问避免编译器将后补的字节优化成旧值。

`usb_state=connected` 表示处理查询时 CDC/DTR 已连接，不表示卡读成功。`nfc_state` 是当时主任务状态：initializing、ready、reading、recovering、update；ready 是代码已进入 Discovery 的快照，**不是实时射频自检结果**。

查询不发 PN7150 命令、不暂停读卡、不进入 MSC。当前保留每秒心跳作为连接确认，但不自动发送旧版 `02` 详情。新版上位机只在用户主动查询等待期间兼容旧 2.3.x 的 `02`；若要停止旧设备自身的自动详情发送，需要更新设备固件。

上位机查询超时约 3 秒，随后恢复操作，不自动重发；MSC-only 模式没有正常 CDC，需要退出更新模式并重新连接才能再查询。

“我已经是最新版，为什么还失败？”应核对 version、built 和 ELF SHA，而不是只看 EXE 版本或 BIN 文件名。确认运行的是同一构建后，再按 RF/会话问题分析；版本识别不能自动修复耦合不佳。

## 15. 真正的 UF2 MSC 固件更新

### 15.1 不是“串口模拟磁盘”

正常 USB 是 HID+CDC；收到更新命令后，应用软件重启，以真正的 **USB Mass Storage Class** 枚举为磁盘。其 sector read/write 回调在 ESP32 内生成 FAT16 文件系统，并识别操作系统写入的 UF2 数据。这不是向 CDC 发 BIN，也不是把板子切到 Espressif ROM 下载器后冒充 UF2。

它是**当前应用实现的 OTA 更新人格**，不是单独常驻的 UF2 bootloader：若当前应用已损坏、无法启动或无法收命令，不能靠它恢复，需要 ROM 下载/完整烧录。

### 15.2 16 MB 分区

| 分区 | 起始偏移 | 大小 | 用途 |
| --- | --- | --- | --- |
| nvs | `009000` | `006000` | 预留 NVS |
| otadata | `00F000` | `002000` | OTA 启动选择数据 |
| phy_init | `011000` | `001000` | PHY 数据 |
| ota_0 | `020000` | `600000`（6 MiB） | 应用槽 0 |
| ota_1 | `620000` | `600000`（6 MiB） | 应用槽 1 |
| storage | `C20000` | `3E0000` | 预留 SPIFFS 分区，当前代码未挂载用于卡号存储 |

地址均为十六进制。更新选择非当前运行的 OTA 槽，每槽大小必须恰好 6 MiB；`Uf2_Update_Boot()` 还检查物理 Flash 至少 16 MB。旧 4 MB/其它布局不满足要求。

`storage` 不用来保存 PC 的 `cards.json`；虚拟卡 JSON 的输出/读取位置仍由上位机和游戏 DLL 管理。

### 15.3 进入模式

1. PC 发送 `30` 空载荷。
2. 有待写/执行写卡时回复 `B0 01`；没有目标 OTA 分区或无法创建任务等进入失败时回复 `B0 02`。
3. 成功安排后回复 `B0 00`，临时任务等约 500 ms，让 CDC 确认先送出。
4. 将一次性标记写入 RTC no-init RAM，调用 `esp_restart()`。
5. 新启动同时满足“软件重启原因 + 正确标记”才进入 MSC；消费后立刻清标记。
6. VEN 拉低，不创建 NFC 主任务；USB 只枚举 MSC，正常 CDC 心跳、HID 刷卡和写卡功能关闭。

进入确认只表示已安排切换，不表示镜像已更新。MSC 模式没有正常 CDC，因此上位机失去原串口是预期行为。

### 15.4 虚拟磁盘

VID=`1CCF`、PID=`5253`；卷标 `SKPASS_UF2`，Board-ID=`SKPASS-S3-N16R8-UF2-V1`。

虚拟容量为 32768×512 字节，即 **16 MiB**；sector size 512、FAT16、一个 FAT、128 个 FAT sector、根目录 32 项。磁盘可见容量不等于应用允许写入 16 MiB，应用镜像上限仍为 6 MiB。

文件：

- `INFO_UF2.TXT`：Board-ID、family、相对地址格式等说明。
- `STATUS.TXT`：READY/已接收块数，或 ERROR，或 COMPLETE。

这是动态生成的文件系统，不是普通持久 U 盘。FAT、目录及 OS 元数据写入被忽略；应用在数据区识别 UF2 sector，不把任意文件作为用户数据保存。拔出后不应期望复制的文件仍像普通 U 盘一样可读。

SCSI inquiry 的旧 revision `2.2` 是当前源码中的更新人格字符串，不是固件真实应用版本；真实版本以 `31/B1` 查询和心跳为准。

### 15.5 本工程接受的 UF2

每个 UF2 block 为 512 字节，payload 为 **256 字节**。当前格式要求：

- 起始 magic：`0A324655`、`9E5D5157`；末尾 magic：`0AB16F30`。
- flags 恰好 `00002000`，带 family ID。
- family ID：`C47E5767`（ESP32-S3）。
- `numBlocks` 为 1～24576，`blockNo < numBlocks`。
- target address 必须为 `blockNo ×256`，即**应用相对偏移从 0 开始**。
- 所有块的总块数一致，不能混合镜像。
- 重复块只有内容相同才接受；冲突重复块报错。

UF2 address **不是**物理 Flash 的 `0x20000/0x620000`。不同工具即使使用同样 ESP32-S3 family，也可能生成全 Flash 地址 UF2，这种文件不能直接用于本更新器。

正常上位机选择的是 `SKPass.bin` 应用镜像，由 PC 校验并转换成此格式，再复制到 MSC 磁盘。只把裸 BIN 拖进磁盘不会触发成功更新；不要选择 bootloader、partition-table、ota_data_initial 或合并整片镜像。进入/复制/轮询状态由上位机完成，固件本身不提供另一个 CDC BIN 流式上传协议。

### 15.6 OTA 接收与校验

首个有效块调用 `esp_ota_begin()` 准备非活动分区；`s_seen` 位图记录块，允许乱序 `esp_ota_write_with_offset()`，只有所有不同块都收到才完成。

随后 `esp_ota_end()` 执行 ESP-IDF 镜像完整性校验，读取应用描述并检查项目名，成功后才 `esp_ota_set_boot_partition()`。配套 PC 端还验证 ESP32-S3 应用格式及摘要。完整性校验/哈希**不是发布者数字签名认证**，本文不将此流程描述为已建立安全启动信任链。

当前 SKPass 更新器接受项目名 **SKPass 或旧 pn7150_cardio**，后者用于回退兼容。旧更新器不一定接受 SKPass，因此回退到旧固件后再次升级仍可能需要完整烧录。

在设置启动分区之前的中断/校验失败不会选中候选应用，原运行固件保持启动选择。但该实现不是“新程序有 bug 会自动回滚”的运行健康检查系统。完成后也不要在同一会话复制第二个镜像；后续错误不应理解为已经自动恢复旧启动选择。

### 15.7 更新操作与退出

1. 连接正常 SKPass CDC，在固件更新页按需获取设备信息。
2. 点击进入 UF2 MSC，等 Windows 枚举 `SKPASS_UF2`。
3. 在上位机选择，或拖入**单个** `.bin`/兼容 `.uf2`。大拖放框、异步读文件/查磁盘/轮询属于 PC UI 实现。
4. 等 `STATUS.TXT` 明确为 `COMPLETE`，才算设备写入和镜像校验成功。复制窗口结束不等于成功；上位机在收到 COMPLETE 前最多显示 95%。
5. **断开 USB 及外部 5 V，确保真正断电，再重新上电。** 此模式不会写完自动重启，也不会因弹出 U 盘就恢复刷卡。
6. 重新连接正常 CDC，点获取设备信息，核对新的 version/built/SHA，再做读卡回归。

更新页完成/失败后按钮恢复可用、侧边栏可切换，都是 PC 端改进；当前仍在 MSC 时“进入模式”不能再次使用正常 CDC 命令。若外部 5 V 仍为系统供电，仅拔 USB 不一定退出模式。

ERROR 时记录 `STATUS.TXT` 和 UART 原因，完整重新上电后再试。不要连续投放不同镜像碰运气；若当前应用/bootloader/分区表损坏，使用 ROM 下载模式重新完整烧录。

## 16. 对话中问题的解释与排障

### 16.1 `CORE_SET_CONFIG` 的 `40 02 01 05` 是什么

按 NXP 的 NCI 定义，`05=SYNTAX_ERROR`，`09=INVALID_PARAM`，不能把它们混为一个“参数值错误”状态。历史 `40 02 01 05` 表示 CORE_SET_CONFIG 返回语法错误。

仅凭 RSP 不能反推出当时完整 TX，也不能证明一定是 `A0 0D` 数值超出范围。应核对原始 TX 的 payload LEN、参数数量、扩展 ID、各参数长度和实际字节数。当前默认电源配置的 payload 明确为 `01 A0 0E 03 06 64 00`，共 7 字节；不把用户未经天线测试的 `A0 0D 03 78 08 00` 当成实测调谐结果。

历史后来的 `40 02 02 00 00` 是配置成功响应（status00），不是读取失败。初始化阶段仍需继续检查 RF_MAP/Discovery，而不能只认“头校验通过”。

### 16.2 `RF_DEACTIVATE/RF_DISCOVER` 状态 `06`

NCI 定义中 `06=SEMANTIC_ERROR`，通常要结合 RF 状态、当前接口、先后命令及通知判断为何命令在该上下文不成立，不能仅认为是随机 I²C 丢包。

此前读到卡后立刻输出假 CARD_REMOVED，再反复发不匹配的去激活/发现，容易使主机和控制器状态不一致。当前处理是区分 RSP/NTF、ISO-DEP 在场检测只用于正确接口、正常去激活直达 Discovery、异常 `06` 显式请求发现或复位。日志“rejected”保留了不准确的文字，本文不照搬成标准状态名。

### 16.3 `CORE_GENERIC_ERROR_NTF status=A1`

NXP 定义为 `ACTIVATION_FAILED`。卡边缘位置、移动、多目标或协议激活失败等都可能相关，但这条通知本身不指明电源/天线/密钥的唯一原因。当前在等待卡阶段记录为调试并继续轮询，不把该通知当作激活数据或 CARD_ID。

若随后可正常激活并读到数据，它不是“控制器必然死锁”的证据；若有明确目标选择后一直等不到激活，则按会话错误恢复。

### 16.4 心跳正常，为什么任何卡都读不了

心跳独立于 NFC 主任务。PN7150 可能处于 Sleep、Idle、信用耗尽、等待迟到数据或错误激活状态，USB 仍然正常。

当前读取/写入失败会报告结果，再丢弃 RF session，2000 ms 后用 VEN 硬复位 PN7150，重新 Reset/Init/Discovery。应看到 RECOVERY→READY；只换上位机 EXE/AimeIO DLL不会安装这个底层恢复逻辑。

如果 READY 后立即同一 UID、READ 又失败，说明状态机在持续重新尝试，不是仅凭重复日志就能判断“ESP32 一直重启”。记录真正的 reset reason/启动头，以及主机 USB 是否重新枚举，可进一步区分。

### 16.5 `ESP_OK raw=100400/10B0` 为什么不是成功

`ESP_OK` 是函数传输结果，READ 的数据还需 18 字节布局和尾状态。3 字节 `100400`、2 字节 `10B0` 都不包含完整块数据；短回应不能拿来抽取 BCD 卡号。认证 `size=2 status=03` 也不是认证成功。

不能从这些短应答判断“卡空白”“锁卡”“Classic 线圈坏”或某一型号；需要结合正确认证、完整块数据、卡位置及对照卡测试。固件/上位机应保留 UID，并明确标注未取得 access code，而不是拿 UID 冒充卡号。

### 16.6 FeliCa 很远可读，Classic 要贴近，加 PETG 壳就失败

**不能据此直接判定“Classic 线圈损坏”。** 常见 PN7150 模块通过同一副主天线工作，具体应查模块原理图；不同卡自身的天线尺寸、谐振、芯片功耗、协议调制/返回信号和持续交易要求可能不同。

读 UID 依赖的防冲突/激活交易，与建立 Crypto1 认证并连续读取三块的数据交易不同。远处能完成短的 UID 交易，不保证整个认证/数据序列都有足够稳定的通信余量；FeliCa 某张卡可读约 5 cm，也不是 Classic 任意卡的距离保证。

用户去壳日志曾出现配置 Key 失败、factory_FF 认证成功、Block 0/1 成功、Block 2 偶发失败，后续又完整成功。这说明该卡在部分条件下能认证并读出数据，不能简单归类为“空白所以肯定读不了”或“密钥始终不对”；仍需排查 RF 余量、会话同步和时序。

普通未填充 PETG 不是金属屏蔽层，但外壳可能增加卡到天线距离、改变摆放/固定压力、改变 PCB 与附近金属/线缆位置；颜料、填充材料和具体结构也不能仅凭“PETG”三个字排除。没有测量前，不把失败全部归因于塑料材质，也不声称修改延时已经修好天线。

建议按固定条件做对照：

1. 同一张卡、同一面/方向、同一中心位置；分别裸机、加与壳等厚的非金属垫片、装壳测试。
2. 比较“UID 成功率”和“完整 48 字节成功率”，不要只计 CARD_ID。
3. 另用一张已知正常的 Classic 卡和一张 FeliCa 卡，排除仅某张卡的差异。
4. 记录 configured/factory_FF、失败块号、每次重选结果及 RECOVERY 次数。
5. 检查 PN7150 电源在 RF 工作时是否稳定，外部 5 V、共地、上拉、电平转换及天线周围金属/线缆位置是否合理。
6. 若需要改 RF 参数，按 NXP 天线设计/调谐流程测量目标模块；不要复制网上 `A0 0D` 常量碰运气。

帧间隔和有界重试只能改善部分偶发丢包/时序问题，不能补偿严重失谐、供电不足、损坏卡、未知密钥或权限拒绝。

### 16.7 同卡反复读导致上位机“卡号→未取得→卡号”闪动

非 ISO-DEP 卡会重新激活，底层一次成功后下一次仍可能读失败并恢复。PC 若把每次 TAG/正在读取/失败都覆盖到卡号预览，容易出现与重试频率同步的闪动。

此前提出过同卡保留结果、防止暂态闪动的需求。当前代码会避免相同完成结果的重复刷新，并按完整数据缓存解析结果；但若同卡本次读成功、下次读失败，仍会更新预览。它并未实现永久保留一次成功来覆盖后续失败。10 秒无新读卡活动会清空预览，历史记录保留；固件继续上报真实状态和诊断。

固件没有把上位机 10 秒清屏计时器做成断 RF/断 USB 指令，也没有为了不闪动而伪造一次成功后的所有后续读取成功。

### 16.8 写卡认证失败 `[stage=1, detail=03]`

这表示写卡在认证阶段失败，没有走到数据写入步骤。先选卡**当前** Key A；空白卡常为 FF，但“空白”不能保证所有卡都用 FF。然后确认同 UID、SAK08、interface80、卡在稳定位置，并查看是否有 RF/重选问题。

不应该通过盲目修改扇区尾块解决：本版本明确禁止 Block 3。被锁定、未知密钥或不允许访问的卡没有保证可写；本固件没有破解或恢复密钥功能。

### 16.9 卡一直放着偶尔不读，要晃动才读到

需要先区分：没有新激活、全局时间节流、长扫描/重试占用、会话恢复中、还是 PC 端没有产生新的游戏呈卡事件。卡边缘位置也可能使一次交易失败，移动后耦合改变才能恢复。

先看 CARD_ID/TAG 是否连续出现、原始数据是否完整、READY/RECOVERY是否交替，再检查 PC DLL 的呈卡策略。不能只看到上位机显示旧卡号就证明游戏每次已经收到新的 CDC 事件。

### 16.10 串口 `Writing to serial is timing out`

该警告来自 PC 端串口写入超时，不是 PN7150 的 NCI 状态码。原生 CDC 是自定义二进制协议，不一定提供交互控制台；UART 日志口与 CDC 应分别确认。检查端口、DTR、游戏/上位机占用及当前是否已切 MSC。

### 16.11 更新成功却仍显示旧固件，或没出现 UF2 磁盘

- 刷错设备/COM、只编译没烧录，或烧的是另一工程的 BIN。
- 更新只完成 PC 文件复制，设备 STATUS 尚未 COMPLETE。
- 外部 5 V 仍供电，仅拔 USB 没有真正退出更新模式。
- 旧固件不支持 MSC/项目名不兼容，或分区表不是 16 MB 双 OTA。
- USB 接到 UART 桥，而不是 S3 原生 USB-OTG。
- 查询结果是旧 PC 快照；重新连接后主动获取并核对 ELF SHA。

SKPass v1.4 发行中的应用镜像为 2.5.0；仅更新 EXE 不能升级设备。旧工程路径/版本仅作迁移背景，不应拿历史 README 中的 BIN 当成当前发行。

## 17. 测试、验收与后续开发

### 17.1 当前已有的软件验证

2026-10-11 的整理记录见 [verification.md](verification.md)：

- ESP-IDF 5.2.5、ESP32-S3、16 MB 构建通过；应用大小及 checksum/SHA 镜像检查通过。
- 固件主机侧 CTest **3/3**：HID/CRC、Classic 读取容错、限定写卡。
- Classic mock 覆盖 **14 个场景**，并曾在 100 Hz/1000 Hz 节拍下验证：认证超时、双密钥回退、单块 NACK/超时/短帧/错误状态、重试耗尽、重选失败、补试认证失败、结果/部分数据保护等。
- 写卡 mock 验证固定 1/2 块、UID 绑定、认证/预读失败无写命令、NACK 停止、回读不一致失败；CRC/HID 验证协议回归。

当前发行记录中固件为 351424 字节（`0x55CC0`），小于 6 MiB 应用槽；重新构建后大小、日期和摘要可能变化。产物文件哈希以 [发行 SHA256SUMS](../releases/v1.0/SHA256SUMS.txt) 为准，不把 ELF SHA 和文件哈希混用。

这些是构建/软件模拟验证。**SKPass 更名整理后的本发行未进行实际烧录、天线距离测量、真实卡写入、UF2 USB 更新或游戏联网验证。** 用户此前真实日志证明旧构建在某些条件下能读卡，不等同本构建全部实机回归已通过。

主机 mock 不覆盖真实 I²C 电气、PN7150 firmware 行为、RF 时序、Crypto1 硬件恢复或真实 MSC/Windows 缓存。不要把测试数目当作硬件兼容保证。

### 17.2 运行固件主机模拟测试

使用普通 PC 的 C 编译器/CMake，而不是 ESP32 交叉编译器；在 `SKPass/esp32` 下：

```powershell
cmake -S tests -B build-host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

若没有 Ninja，可使用已安装的合适生成器；多配置生成器需为 build/ctest 指定一致的 `--config Debug` / `-C Debug`。

单独用 GCC 复跑 Classic 14 场景和严格警告检查：

```powershell
New-Item -ItemType Directory -Force build-mifare-tests | Out-Null
gcc -std=c11 -Wall -Wextra -Werror -I tests/mifare_stubs -I main tests/mifare_reader_tests.c main/mifare_reader.c main/aime_reader.c -o build-mifare-tests/mifare_reader_tests.exe
./build-mifare-tests/mifare_reader_tests.exe
```

加 `-DportTICK_PERIOD_MS=1` 可验证 1000 Hz 模拟。`tests/mifare_stubs` 只供主机测试，不能放入实际 ESP-IDF 组件 include path。

### 17.3 建议的实机验收顺序

| 验收项 | 操作 | 应观察的证据 |
| --- | --- | --- |
| 身份 | 首次完整烧录后连接原生 USB | 新心跳、31/B1 的 2.5.0、正确构建 SHA |
| 初始化 | 无卡上电 | ACK→Reset/Init/Config/Map/Discovery→READY |
| 无卡等待 | 放空数分钟 | 心跳持续，不因普通等待超时周期性 VEN 复位 |
| Classic 成功 | 已知密钥的自有卡，固定位置 | 认证成功、三块完整、mifare_len48 |
| 弱耦合恢复 | 适度移动/移卡造成失败，再放正常卡 | 有界补试或 RECOVERY→READY，后续能读，无需拔 USB |
| 失败不冒充 | 未知密钥/不可读卡 | UID 可见，但状态失败，未取得 access code，不报空白 |
| 重复刷卡 | 同卡离开再进入、不同卡交替 | 无永久 UID 去重，注意默认 3 秒全局节流 |
| 长时间在场 | Classic/FeliCa 保持放置 | 根据协议重复激活；不伪造 ISO-DEP 离场，UI 不掩盖真实错误 |
| FeliCa | 各品牌真实卡逐一测试 | 服务字节、82/00原始内容/状态、PMm、PC 解码与卡背逐位核对 |
| HID 回归 | 原 CardIO 测试工具 | 9 字节格式、ID/字节序、抑制间隔正确 |
| CDC 重连 | 断开/重连 PC 端 | 心跳重新确认，不复活旧待写请求 |
| 限定写卡 | 可接受覆盖的自有 1K 测试卡 | 仅 Block1/2，status5、UID及回读32字节匹配 |
| 更新 | 兼容应用 BIN/UF2 | MSC 出现→STATUS COMPLETE→完整断电→新版本查询 |
| 游戏 | PC 端匹配位数 DLL/INI/JSON | 单独检查实体/虚拟呈卡，避免同 CDC 被上位机占用 |

每次记录固件版本/摘要、板型、PN7150 模块/电源、是否装壳、卡协议/SAK、位置和失败环节。对外分享需脱敏 UID/IDm、卡号、密钥及完整块内容；不要只截取最后一条“检查密钥和连接”。

### 17.4 后续开发应遵守的约束

- 所有 PN7150 命令由同一 NFC 任务执行，新增 PC 命令先队列化，不在 USB 回调抢总线。
- 新的存在检测必须按协议实现和验证，不能把 500 ms 计时器当物理离卡。
- 扩大 Classic 读取范围时要明确权限/扇区密钥、认证边界、总时延；当前三块容错不能自动等同全卡扫描。
- 新写卡功能必须另行确定覆盖范围及授权；不要暗中突破 Block1/2 或尝试写 trailer。
- 需要更强 UF2 恢复/签名校验时另行设计；当前应用 OTA不等于独立恢复 bootloader或自动回滚。
- 修改 CDC payload/消息应保持协议兼容或显式升级版本，同时回归 Qt 与 x86/x64 AimeIO；不要直接发送带 padding 的结构体。
- 变更天线/外壳/供电后重新做 RF 对照测量，不把网络复制来的配置当成调谐结论。

## 18. 参考资料、历史文档与许可

### 18.1 协议来源

源码行为的优先依据是 `esp32/` 当前实现；外部示例用于解释协议/设计来源，不代表已完整移植其所有功能。2026-10-11 文档核对中重新读取了 ElectronicCats 读块示例及驱动相关部分、NXP 公开 NCI 状态定义：

- [ElectronicCats-PN7150 仓库](https://github.com/ElectronicCats/ElectronicCats-PN7150)：NXP 衍生驱动、I²C/NCI、供电配置及 readerTagCmd/readerReActivate 等流程。
- [MifareClassic_read_block](https://github.com/ElectronicCats/ElectronicCats-PN7150/blob/master/examples/MifareClassic_read_block/MifareClassic_read_block.ino)：嵌入 Key A 的 `40 sector 10 key`、`10 30 block` 及尾状态检查。
- [MifareClassic_write_block](https://github.com/ElectronicCats/ElectronicCats-PN7150/blob/master/examples/MifareClassic_write_block/MifareClassic_write_block.ino)：本工程写卡历史实现引用的两阶段写入来源。
- [ElectronicCats 主驱动](https://github.com/ElectronicCats/ElectronicCats-PN7150/blob/master/src/Electroniccats_PN7150.cpp)：PN7150 外部 5 V 配置、Sleep/Select、重新 Discovery。上游也含 PN7160 路径，必须分芯片看。
- [NXP linux_libnfc-nci：nci_defs.h](https://github.com/NXPNFCLinux/linux_libnfc-nci/blob/master/src/libnfc-nci/hal/include/nci_defs.h)：核对 `OK=00`、`REJECTED=01`、`SYNTAX_ERROR=05`、`SEMANTIC_ERROR=06`、`INVALID_PARAM=09`、`ACTIVATION_FAILED=A1`。
- [NXP 官网](https://www.nxp.com/)：以 PN7150 产品资料、适用修订的用户手册/数据手册和 NFC 天线设计调谐指南为电气/RF依据。本文没有用未查阅的手册章节或寄存器含义推断某次天线故障。
- [ESP-IDF 5.2.5 / ESP32-S3 文档](https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/index.html)：构建、USB、I²C、OTA 与目标芯片背景；具体 API 以锁定工具链为准。
- [UF2 格式规范](https://github.com/microsoft/uf2)：512 字节块、magic/family 等格式；本工程另加固定 payload/应用相对地址限制。

卡体系/PC侧解析背景：

- [Amusement IC](https://sega.bsnk.me/allnet/amusement_ic/)。
- [Classic AiMe](https://sega.bsnk.me/allnet/access_codes/classic/)、[Banapass](https://sega.bsnk.me/allnet/access_codes/bana/)、[Amusement IC access code](https://sega.bsnk.me/allnet/access_codes/aicc/)。
- [AiMeDB intro](https://sega.bsnk.me/allnet/aimedb/intro/)、[communication](https://sega.bsnk.me/allnet/aimedb/communication/)、[common](https://sega.bsnk.me/allnet/aimedb/common/)、[felica](https://sega.bsnk.me/allnet/aimedb/felica/)、[basic account](https://sega.bsnk.me/allnet/aimedb/basic_account/)。这些是此前用户提供的社区逆向资料，不是 NXP 官方电气规格，也不表示本固件直连 AiMeDB。

### 18.2 专项文档和历史语境

- [Classic 读取重试](mifare-retry.md)：延时、Sleep/Select、mock 场景。
- [写卡](writing.md)：固定块布局、请求/结果、使用风险。
- [NFC 恢复](nfc-recovery.md)：USB存活与RF存活的区分、恢复路径。
- [FeliCa 实验](felica-experiment.md)：待实机测试的记录模板，不是已通过的真卡报告。
- [设备查询](device-identity.md)：按需按钮、JSON字段、旧版兼容。
- [固件更新](firmware-update.md)：MSC模式及首次部署。
- [命名迁移](branding.md)：为何保留 CardIO兼容常量/旧项目回退。
- [SKPass 验证记录](verification.md)。

部分专项文档保留功能最初加入时的版本/命名，例如 `2.1-write`、`2.2`，或历史 `build/` 路径；它们是开发记录，不应覆盖本 README 的当前 2.5.0 配置。尤其 NCI `06` 的标准名称以本 README 和上游定义为准。

### 18.3 开源发布前的许可事项

请阅读根目录 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。CardIO/PN5180-cardio 来源、ElectronicCats/NXP 衍生协议参考、ESP-IDF/TinyUSB及 PC侧 SPAD/Classic算法来源应保留，不因 SKPass 更名而抹去归属。

当前自有代码尚未由项目所有者选定统一开源许可证，部分第三方来源授权仍待核实。工程整理和 README 完成不等于全部代码已可按 MIT 或其它协议公开重新许可。正式上传/分发前须核对各来源授权、必要声明，并排除真实卡号、密钥、个人 JSON 和生成缓存。

---

维护提示：修改固件行为时，同步更新 `version.txt`、本 README、专项文档及发行验证记录。**读到 RF 标识、取得完整卡内数据、解析 access code、游戏成功呈卡、联网账号有效，是不同层次的结果，排障时逐层验证。**
