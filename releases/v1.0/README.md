# SKPass v1.0

唯一发行版本：上位机 v1.0、ESP32-S3 固件 1.0.0、AimeIO API 1.0。包含当前读卡、写卡、高级 NFC 读写与扫描、Type 4 NDEF / APDU 卡模拟、UF2 更新及 ROM 下载功能。

| 文件 | 用途 |
| --- | --- |
| [desktop/SKPass-v1.0.exe](desktop/SKPass-v1.0.exe) | Windows x64 单文件上位机，内置 Qt、esptool 和游戏 DLL |
| [firmware/SKPass.bin](firmware/SKPass.bin) | 应用固件，供 UF2 / ROM 更新使用 |
| firmware/first-flash/ | 配套 bootloader、分区表及 OTA 初始数据 |
| aimeio/x86/aimeio_skpass.dll | 32 位游戏 DLL |
| aimeio/x64/aimeio_skpass.dll | 64 位游戏 DLL |

首次部署或修复请使用上位机 ROM 下载页的首次烧录模式，选择本包应用 BIN；该模式同时写入配套启动文件。已有 SKPass 双 OTA 布局可使用 UF2 或 ROM 应用更新。ROM 烧录后按提示重新插拔 USB。旧设备的 UF2 自动重启行为取决于当前运行更新器的 AutoRestart 能力。

EXE 运行库解包至同级 SKPass-v1.0-runtime/payload-*/。偏好设置位于 HKCU\Software\SKPass\SKPass v1.0；首次正常启动继承此前 v1.9 开发版的 JSON/游戏目录设置，不复制或删除个人卡片文件。

详细使用见 [项目说明](../../README.md)、[固件更新](../../docs/firmware-update.md)、[卡模拟](../../docs/card-emulation.md)。验证结果见 [VERIFICATION.md](VERIFICATION.md)，文件校验见 [SHA256SUMS.txt](SHA256SUMS.txt)。
