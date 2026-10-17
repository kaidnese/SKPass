# SKPass v1.0

唯一发行版本：上位机 v1.0、ESP32-S3 固件 1.0.0、AimeIO API 1.0。包含当前读卡、写卡、高级 NFC 读写与扫描、Type 4 NDEF / APDU 卡模拟、UF2 更新及 ROM 下载功能。

| 文件 | 用途 |
| --- | --- |
| [desktop/SKPass-v1.0.exe](desktop/SKPass-v1.0.exe) | Windows x64 单文件上位机，内置 Qt、esptool 和游戏 DLL |
| [firmware/SKPass.bin](firmware/SKPass.bin) | 应用固件，供 UF2 / ROM 更新使用 |
| firmware/first-flash/ | 配套 bootloader、分区表及 OTA 初始数据 |
| aimeio/x86/aimeio_skpass.dll | 32 位游戏 DLL |
| aimeio/x64/aimeio_skpass.dll | 64 位游戏 DLL |
| [enclosure/](enclosure/README.md) | 3D 打印外壳模型（全封闭与半封闭 STL 文件） |

外壳模型提供两种 3D 打印方案（见 [外壳说明](enclosure/README.md)）：
* **全封闭**：整机全包裹美观，但外壳壁厚导致**刷 Classic 卡效果不佳**；
* **半封闭**：**露出 PCB 的背面**，卡片可以直接贴着天线背面刷，显著改善 Classic 兼容卡识别率。

首次部署或修复请使用上位机 ROM 下载页的首次烧录模式，选择本包应用 BIN；该模式同时写入配套启动文件。已有 SKPass 双 OTA 布局可使用 UF2 或 ROM 应用更新。ROM 烧录后按提示重新插拔 USB。旧设备的 UF2 自动重启行为取决于当前运行更新器的 AutoRestart 能力。

EXE 运行库解包至同级 SKPass-v1.0-runtime/payload-*/。偏好设置位于 HKCU\Software\SKPass\SKPass v1.0；首次正常启动继承此前 v1.9 开发版的 JSON/游戏目录设置，不复制或删除个人卡片文件。

虚拟卡管理支持删除已保存卡片：选中卡片后点击「删除选中卡片」并确认，立即同步当前使用的 `cards.json`。删除已启用卡时同时停用虚拟卡；写入失败时保留原卡片和启用状态。

详细使用见 [项目说明](../../README.md)、[固件更新](../../docs/firmware-update.md)、[卡模拟](../../docs/card-emulation.md)。验证结果见 [VERIFICATION.md](VERIFICATION.md)，文件校验见 [SHA256SUMS.txt](SHA256SUMS.txt)。
