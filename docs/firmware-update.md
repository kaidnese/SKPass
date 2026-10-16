# SKPass 固件更新：UF2 与 ROM 串口下载

当前版本为上位机 **v1.0**、固件 **1.0.0**，目标 ESP32-S3-WROOM-1-N16R8。正常运行保留 CardIO HID、数据 HID 与兼容 CDC。UF2 更新仍为真正的 MSC 磁盘；ROM 下载切换到芯片自带下载程序，支持原生 USB 和外接 UART0。两种更新模式都暂停读卡，不能同时使用 HID 读卡。

发行文件在 [v1.0](../releases/v1.0/README.md)。以下 UF2 自动重启能力说明适用当前固件；软件进入 ROM 的操作见本文末节。

## 首次部署

从旧 pn7150_cardio 迁移时需完整烧录一次，旧更新器按项目名拒绝 SKPass 镜像。配套应用与启动文件在 `../releases/v1.0/firmware/`，也内置于上位机 ROM 首次烧录模式，见 [发行 README](../releases/v1.0/README.md)。已有 SKPass 双 OTA 布局可直接 UF2 更新到 1.0.0；早期不支持自动重启的更新器本次仍需手动复位。

UF2 更新依赖本工程的 16 MB 分区表和双 OTA 分区。第一次使用前，必须用 ESP-IDF 完整烧录工程，不能只烧录 `build/SKPass.bin`：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py flash
```

这会同时写入 bootloader、分区表和应用。之后上位机可以选择 `build/SKPass.bin`，自动转换为本工程使用的应用相对地址 UF2。

## 更新步骤

1. 启动 `SKPass-v1.0.exe`，连接带 `-uf2-` 标识的 SKPass；数据 HID 与兼容 CDC 均支持更新命令。
2. 在“固件更新”页面点击进入 UF2 MSC 模式。
3. 等待 Windows 出现名为 `SKPASS_UF2` 的磁盘，选择 `SKPass.bin` 或本工程生成的 `.uf2`；也可将单个 `.bin` / `.uf2` 文件拖入固件更新页面，松开后开始校验并更新。
4. 只有磁盘中的 `STATUS.TXT` 变成 `COMPLETE` 才算成功。
5. 当前更新器（v1.0 满足）在电脑读到 `COMPLETE` 后约 2 秒设备自动断开 USB 并重启，上位机自动重连，无需拔插或断开外部 5 V。若直接用资源管理器复制且未读取状态，设备成功后约 17 秒自行重启。

**自动重启由当前运行的更新器执行，不是由待启动的新镜像执行。** 第一次从不支持自动重启的早期固件更新时，仍需按硬件复位键一次；没有复位键时断开 USB 和外部 5 V 再上电。上位机根据磁盘中的能力行显示对应提示，不会把旧设备当成支持自动重启。

更新写入非活动 OTA 分区；传输中断或校验失败时，旧固件仍保持为启动固件。固件会校验 ESP32-S3 镜像、SHA-256、项目名 `SKPass`、UF2 family ID `0xC47E5767`、应用相对地址、块数和重复块。

上位机的固件读取、磁盘查找和状态检查都在后台执行；更新时可以切换侧边栏。传输进度最多显示 95%，收到设备 `COMPLETE` 后才显示 100% 和更新成功。成功或失败后恢复选择固件按钮；进入更新模式按钮需要重新连接正常 HID/CDC 固件后才可用。等待模式切换期间及更新过程中不接受重复拖入文件。

## 限制和故障处理

- 不要选择 `bootloader.bin`、`partition-table.bin` 或 ESP-IDF 合并镜像；上位机会拒绝它们。
- UF2 磁盘只接受 256 字节 payload、地址从应用分区偏移 0 开始的本工程 UF2。
- 如果没有出现磁盘，确认固件包含 UF2 功能并完整断电重连。当前固件原生内置 UF2；不支持的软件可手动进入 ROM 后完整烧录。
- 命令请在 `esp32/` 下执行，`build/SKPass.bin` 也是该目录的相对路径。
- 如果 `STATUS.TXT` 显示 `ERROR`，旧固件不会被切换，也不自动重启；按复位键或重新断电后可再次进入更新模式尝试。`Not a pn7150_cardio application image` 说明旧更新器拒绝 SKPass 工程名，需先完整烧录迁移，重命名文件无效。
- 本更新路径未实现独立的恢复 bootloader。若 bootloader 本身或分区表损坏，需要使用 ROM 下载模式和 ESP-IDF/esptool 重新完整烧录。

## USB 身份

正常模式保持 VID `0x1CCF`、PID `0x5252`（两个 HID + 可选 CDC）；UF2 模式使用 VID `0x1CCF`、PID `0x5253`（MSC）。HID 连接与会话见 [USB HID 说明](usb-hid.md)。

## ROM 串口下载

### 用户操作

1. 打开「固件更新 → ROM 串口下载」，选择 **原生 USB** 或 **外接 UART0**。
2. 正常连接含 `-rom-` 标识的固件（当前 1.0.0 满足）时，点「进入 ROM 下载模式」。自定义确认窗解释读卡暂停、正常 USB 退出、下载串口可能换号、UART 电平和退出方法。正在写卡时固件拒绝切换。
3. 等待串口重新枚举。原生 USB 可自动寻找一台 ESP32-S3（VID `303A`，ROM USB-OTG PID `0009`，USB Serial/JTAG PID `1001`）；软件切换前已有的 Espressif 端口被排除，避免误选其它开发板。PID `0002` 是 ESP32-S2，不作为本工程的自动候选。出现多台候选时必须指定端口。UART0 需要用户明确选择转串口 COM。
4. 选择或拖入本工程的应用 BIN / 兼容 UF2；拖入只选择并校验。点「开始烧录」后再次确认端口、文件和覆盖范围。
5. 日志显示连接、检查、写入和摘要校验。校验及启动记录提交完成后显示 **「串口烧录与校验完成。请重新插拔 USB」**，进度为 100%，恢复操作按钮。ROM 串口下载不发送自动复位命令，也没有退出 ROM 按钮。
6. 重新插拔 USB 后，上位机自动寻找正常读卡器；收到正常 HID/CDC 心跳才显示已重连。等待没有 20 秒倒计时，重新插拔提示持续保留。新版本信息仍由用户点「获取设备信息」查询。

**设备管理器显示驱动错误时，先看是哪一个接口。** ESP32-S3 的 ROM USB-OTG 是 CDC + DFU 复合设备。Windows 的「USB 串行设备 (COM…)」使用系统 `usbser`，是本功能实际使用的通道；另一个 DFU 接口可能显示错误码 28（未安装驱动），这不影响正常 COM 串口下载。上位机只使用 esptool，不需要给 DFU 安装 WinUSB。若 COM 接口本身也有错误或设备描述符读取失败，则需要处理该接口的驱动／USB 连接，不能按 DFU 提示忽略。接口区别及 Windows DFU 驱动说明见 [Espressif 官方 DFU 文档](https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-guides/dfu.html#usb-drivers-windows-only)。

工具内置在 EXE 中，无需 Python。普通更新也支持只接 TX/RX/GND、没有 DTR/RTS 自动下载电路的转串口模块：软件命令先进入 ROM，下载工具只负责写入和校验，完成后按界面提示重新插拔 USB。

### UART0 接线与手动恢复

| ESP32-S3 | USB 转串口 |
| --- | --- |
| GPIO43 / TXD0 | RX |
| GPIO44 / RXD0 | TX |
| GND | GND |

使用 **3.3V 逻辑电平**，以开发板原理图确认引脚。固件启动日志使用的也是 UART0；ROM 模式由下载程序接管它。

固件版本低于 2.7.0、应用损坏或无法连上 HID 时：按住 BOOT（GPIO0 拉低），按一下 RESET/EN，再松开 BOOT。GPIO46 必须悬空或为低电平。然后选择实际下载串口并烧录；不要求能收到正常固件心跳。带自动下载电路时，也可使用外部 esptool 的标准 DTR/RTS 流程。

没有执行烧录、想退出时：**重新插拔 USB**，然后用正常连接入口重连。ROM 启动不是给现有 USB 额外加一个应用 CDC，而是切换 USB 所有者和设备身份。原生 USB ROM 自动复位在本设备实测未能稳定恢复，当前流程已取消这一步。

### 两种烧录范围

| 范围 | 写入内容 | 保留 / 限制 |
| --- | --- | --- |
| 应用更新（默认） | 备用 OTA 槽；校验后写一份新 OTA 启动记录 | 保留原应用与另一份启动记录；要求有效 SKPass 16MB 双 OTA 分区表 |
| 首次烧录／修复 | `0x0` bootloader、`0x8000` 分区表、`0xF000` OTA 初始数据、`0x20000` 应用 | 使用本发行包配套启动文件；重置启动选择；中断可能需再次完整烧录 |

普通更新先读取分区表与两份 OTA 记录，检查分区表 MD5、地址、容量、加密标志、序号、状态及记录 CRC。无法确认当前启动槽、状态为待验证、序号溢出或布局不符时拒绝写入。初始两份记录全 FF 时视为 ota_0 启动，首次普通更新写 ota_1。未通过应用摘要校验不会提交新启动记录。这里的 `verify_flash` 比较设备 Flash 与源文件的 MD5，不是把整个应用逐字节传回电脑。没有启动健康检查或自动回滚。

首次烧录会覆盖启动文件，不能按普通更新的失败保护来理解。不会整片擦除，不主动擦 NVS/storage；从其它分区布局迁入时，旧数据也不保证能被新固件继续使用。此功能不支持自定义任意 Flash 地址、任意板型、加密镜像或 eFuse 设置。

### 协议与实现

- PC 命令 `34`：单字节载荷，`00` 原生 USB、`01` UART0；两者进入相同 ROM 下载器，选择影响上位机端口处理。
- 设备回复 `B4`：一字节状态，`00` 已安排切换、`01` 正在写卡、`02` 无法安排／已有更新请求、`03` eFuse 禁用了下载或软件强制下载。只读 eFuse，绝不修改。
- 固件 `rom_download.c`：设置请求标志供 NFC 主循环暂停；保留约 600ms 发送 ACK，拉低 PN7150 VEN、断开 TinyUSB，再等待 100ms；设置 RTC `FORCE_DOWNLOAD_BOOT`，执行数字系统复位让 ROM 和默认 USB PHY 接管。
- 上位机 `RomDownload.cpp`：所有命令固定 `--chip esp32s3`、115200、`--no-stub`、`--before no_reset`、`--after no_reset`；检查 16MB Flash，执行有超时的读写和 `verify_flash`。不上传 RAM stub，不写复位寄存器，不发送看门狗或 RTS 复位。使用 ROM 协议避免 stub 进程关闭后下一个命令无法配置原生 USB 端口。
- 固件 2.7.1 的 `skpass_usb.c`：TinyUSB 初始化成功后执行 `tud_disconnect()` → 等待 500ms → `tud_connect()`，正常 HID/CDC 与 UF2 MSC 都使用这段启动流程。ROM 和应用描述符不同，同一 USB 供电下复位时需要电脑重新枚举；该等待用于加固 USB 身份切换，不是修改 ROM 描述符或安装驱动。
- 普通更新从备用槽写入，成功校验后才提交 OTA；失败不自动重发。首次烧录和 OTA 启动记录提交本身若中断，需依实际日志恢复，不能宣称已经成功。
- 耗时操作在后台线程，界面可切换侧边栏；更新过程中禁止重复操作、切换连接和正常关闭窗口，结束后恢复按钮。成功只表示“已写入、校验并选定”，重新插拔后是否启动由正常固件心跳确认；失败显示日志和重新插拔提示。

运行库放在 EXE 同目录 `SKPass-v1.0-runtime/payload-*/`；ROM 临时镜像、读取结果以及 esptool 自解包目录位于其 `rom-work/update-*/`，操作结束后清理。子进程 TEMP/TMP/TMPDIR 显式指向该目录，不解包到 C 盘默认 TEMP。JSON 的默认和自定义位置保持原功能。

### 官方依据与验证边界

参考 [ESP-IDF 5.2.5 USB OTG Console](https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-guides/usb-otg-console.html)、[esptool Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)、[ESP-IDF usb_console.c](https://github.com/espressif/esp-idf/blob/v5.2.5/components/esp_system/port/usb_console.c)、[OTA CRC/选择实现](https://github.com/espressif/esp-idf/blob/v5.2.5/components/bootloader_support/src/bootloader_common_loader.c) 和 [esptool 4.12.0](https://github.com/espressif/esptool/tree/v4.12.0)。

2026-10-13 使用实际 COM49 的 ROM 原生 USB 将 2.7.1 写入备用槽 `0x620000`，应用与启动记录 MD5 校验通过；手动恢复后独立 HID 查询确认固件为 2.7.1、NFC ready。默认 stub 曾导致后续端口无法配置，因此全部命令改为 `--no-stub`。多种自动复位方式未能稳定恢复 HID，按当前方案取消，界面改为提示重新插拔 USB。外接 UART0 下载仍需实机验收。构建和模拟测试不替代硬件验证，完整记录见 [验证文档](verification.md)。若 eFuse 禁用了某种下载方式，软件无法恢复该能力。
