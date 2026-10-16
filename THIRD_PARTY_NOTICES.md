# 第三方参考与数据来源

版本适用说明：本声明适用于 **SKPass v1.0 / 固件 1.0.0**，并保留此前版本的来源记录。当前包装路径为 `releases/v1.0/`、`SKPass-v1.0.exe` 与 `SKPass-v1.0-runtime\payload-<指纹>\`；动态库替换机制不变。Windows HID 使用系统 `hid.dll` / SetupAPI，没有引入 HIDAPI。

智能 NFC 探测器用公开协议事实核对报文布局：nfcpy `src/nfc/tag/tt3.py`（EUPL-1.1）与 Metrodroid 的 T-Union、PAY.SZT、Suica、Octopus 模块（GPL-3.0-or-later），由 SKPass 独立实现有界只读探测与 JSON 编码，未整体复制上游读卡器。来源文件和关键字段见 [docs/nfc.md](docs/nfc.md)。

v1.6 新增未修改的 **Espressif esptool 4.12.0 Windows x64** 独立工具，许可 **GPL-2.0-or-later**。来源与对应源码：<https://github.com/espressif/esptool/tree/v4.12.0>；官方发行：<https://github.com/espressif/esptool/releases/tag/v4.12.0>。版权包含 Fredrik Ahlberg、Angus Gratton、Espressif Systems (Shanghai) CO LTD 及上游贡献者，完整原 LICENSE 与 README 随 EXE 内的 `tools/esptool/` 打包，发行包另附 `LICENSE.esptool.GPLv2.txt`。准备脚本固定官方 ZIP SHA256，见 `desktop/tools/esptool/README.md`。下载器运行使用 EXE 附近目录，不使用系统 C 盘默认 TEMP。

* `desktop/src/SpadTables.h` 的2304个表项来自用户提供的 `micetools-ref/spad.c`：Bottersnike/micetools，`src/micetools/lib/mice/spad.c`，符号 `SPAD0_SPN_TABLE`。与本地来源逐项比对一致。AimeIO共享该文件，不因SKPass改名改变其来源。公开来源：<https://gitea.tendokyu.moe/Bottersnike/micetools/raw/branch/master/src/micetools/lib/mice/spad.c>。
* SPAD/Classic算法依据用户提供的 `aime-spad.py`、`aime-classic-code.py` 与 micetools 的 spad/solitaire/felica 实现移植；Classic计算器参考 <https://blog.naivetomcat.cn/pages/aimecalc/>，KEY取源码的 `A1B3E86CF02974D6`。
* PN7150 NCI DATA封装与MIFARE命令依据 ElectronicCats/NXP衍生实现，作者 Salvador Mendoza 与 Electronic Cats：<https://github.com/ElectronicCats/ElectronicCats-PN7150>，特别是 `examples/MifareClassic_read_block/MifareClassic_read_block.ino`。其示例头标记 beerware、无担保；本工程以其为协议行为参考，没有整体复制该示例。
* AiMe/AICC背景与DFC映射参考用户文档和 <https://sega.bsnk.me/allnet/amusement_ic/>。
* 2.1-write的两阶段MIFARE写入依据同仓库 `examples/MifareClassic_write_block/MifareClassic_write_block.ino`，使用PN7150分支的尾状态00与固定 `10 A0 block` / `10 data16` 流程；重新激活依据其 `readerReActivate`，使用实际discovery ID。

提供的 micetools 本地片段未包含完整仓库授权文件，本次尝试获取仓库根 LICENSE 返回404。**没有将第三方表项擅自重新许可为MIT或其它协议。对外发布/分发前需要核实 micetools 对应版本的授权条件并保留所需版权与许可声明。** 本说明不替代上游许可证。

---

## `micetools` 使用位置、上游授权核查与版权声明

### 1. 本工程使用 `micetools`（及配套文档 `sega.bsnk.me`）的具体位置

| 本工程文件 | 涉及符号 / 功能 | 对应上游来源（`Bottersnike/micetools` & `sega.bsnk.me`） | 使用方式 |
|---|---|---|---|
| `desktop/src/SpadTables.h` | `SpadTables`（9×256 = 2304 字节 S 盒表） | `src/micetools/lib/mice/spad.c` 中的 `SPAD0_SPN_TABLE`；同见 <https://sega.bsnk.me/allnet/amusement_ic/> | 逐字节原样提取常量表项（C++ `std::array` 封装） |
| `desktop/src/Spad.cpp`<br>`desktop/src/Spad.h` | `Spad::encrypt`、`Spad::decrypt`、`Spad::accessCode`、`Spad::tablesValid` | `src/micetools/lib/mice/spad.c`（`Spad0Encrypt` / `Spad0Decrypt`）、`src/micetools/dll/devices/nfc/felica.c`（`FelicaPopulate` 偏移 `6..15` BCD 布局）及 <https://sega.bsnk.me/allnet/amusement_ic/> | 算法用 C++/Qt 重写实现，包含 `SpadTables.h` |
| `aimeio/spad.cpp`<br>`aimeio/spad.h`<br>（由 `aimeio/aimeio.c` 调用） | `spad_access_code` | 同上（共享 `../desktop/src/SpadTables.h`） | C++17 独立实现 S_PAD0 解密与 BCD 提取，供 x86/x64 `aimeio.dll` 离线解码 |
| `desktop/src/AimeCode.cpp`<br>`desktop/src/AimeCode.h` | 匿名命名空间 `MiniSolitaire`、`AimeCode::digest`、`fromSerial`、`serialFromCode` | `src/micetools/lib/mice/solitaire.c`（`SolitaireCipherEncode` / `Decode` 及 22 张牌洗牌逻辑）与 <https://sega.bsnk.me/allnet/access_codes/classic/>（以及 <https://blog.naivetomcat.cn/pages/aimecalc/>） | 算法用 C++/Qt 重写实现（未直接复制上游 C 源码） |
| `desktop/src/CardTypes.h` | `issuerFromDfc`、`issuerFromCode` | `src/micetools/dll/devices/nfc/felica.c`（`DFC_*` 常量定义）、`felica.h`（`FelicaAICVendor`）及 <https://sega.bsnk.me/allnet/amusement_ic/> | 依据公开协议规范编写发行方识别映射 |
| `desktop/src/MainWindow.cpp`<br>`desktop/tests/core_tests.cpp` | 调用 `Spad::*`、`AimeCode::*`、`issuerFromDfc` 及测试向量验证 | 间接使用上述模块 | 调用本地封装接口与回归测试 |

### 2. 上游仓库与授权条件核实结论

经在线核查上游仓库 `Bottersnike/micetools`（<https://gitea.tendokyu.moe/Bottersnike/micetools>）完整文件树（`/tree-list/branch/master`）及相关源码文件：

1. **上游版本信息**：
   - 仓库地址：<https://gitea.tendokyu.moe/Bottersnike/micetools>
   - 配套技术文档：SEGA Arcade Docs（<https://sega.bsnk.me/allnet/amusement_ic/>、<https://sega.bsnk.me/allnet/access_codes/classic/>）
   - 作者 / 版权持有人：`Bottersnike`（Tendokyu）
   - 核查分支与提交：`master` 分支（仓库最新提交 `05fcc7f4f888d0b23401b0b1222816c3673b52d3`，2023-08-30；其中 `spad.c`、`solitaire.c`、`felica.c` 对应提交为 `50737e60116941b6cf7a4753b16f9e30d12b98af`，2023-05-19）。
2. **许可证状态核查结果**：
   - `Bottersnike/micetools` 仓库根目录及 `src/micetools/` 目录下**确实不存在 `LICENSE`、`COPYING` 或 `NOTICE` 文件**（因此请求根 `LICENSE` 返回 404 属于上游未放置该文件，而非网络故障；仓库内仅第三方子项目 `subprojects/cimgui_dep`、`subprojects/detours`、`subprojects/inih_dep` 带有各自许可证）。
   - `spad.c`、`spad.h`、`solitaire.c`、`solitaire.h`、`felica.c`、`felica.h`、`README.md` 与 `meson.build` 均**未包含 SPDX 许可证标识或开源授权条款**，`README.md` 声明该项目为 Ring 系列硬件模拟与研究项目（*"This is a research project, first and foremost"*）。
   - 根据著作权法通则，未附带显式开源许可证的公开代码仓库默认适用**“保留所有权利（All Rights Reserved）”**。本工程除 `desktop/src/SpadTables.h` 提取了用于 Amusement IC `S_PAD0` 互操作性的 2304 个 S 盒数值表项（`SPAD0_SPN_TABLE`）外，其余加解密与卡号换算逻辑均基于公开算法规范由 C++/Qt 重新编写，未直接拷贝 `micetools` 的 C 函数实现代码。

### 3. `micetools` 版权与归属声明文本（随源码与发行包保留）

在源码分发、二进制发布包（含上位机 `SKPass.exe` 与 `aimeio.dll`）及随附文档中，须完整保留以下声明文本，且**不得将 `desktop/src/SpadTables.h` 中的表项标记或重新许可为 MIT 或其它自选开源协议**：

```text
================================================================================
Third-Party Notice: Bottersnike / micetools & SEGA Arcade Docs
================================================================================

Component(s) in this project:
  - desktop/src/SpadTables.h (SPAD0 9x256 S-Box lookup table constants, shared
    by desktop/src/Spad.cpp and aimeio/spad.cpp)
  - Algorithm & protocol references in desktop/src/Spad.cpp, aimeio/spad.cpp,
    desktop/src/AimeCode.cpp, and desktop/src/CardTypes.h

Upstream Source:
  - Repository: https://gitea.tendokyu.moe/Bottersnike/micetools
  - File: src/micetools/lib/mice/spad.c (symbol: SPAD0_SPN_TABLE,
    commit 50737e60116941b6cf7a4753b16f9e30d12b98af / master 05fcc7f4f8)
  - Reference Documentation: https://sega.bsnk.me/allnet/amusement_ic/
    and https://sega.bsnk.me/allnet/access_codes/classic/

Copyright & Attribution:
  Copyright (c) 2022-2024 Bottersnike (Tendokyu).

License Status & Conditions:
  The upstream Bottersnike/micetools repository is published as a public
  research project without a repository-wide open-source LICENSE file (default
  copyright / All Rights Reserved by the upstream author, except where data
  constitutes interoperability constants or uncopyrightable facts).

  1. The 2304 S-Box byte values in `desktop/src/SpadTables.h` are extracted
     from `SPAD0_SPN_TABLE` in `Bottersnike/micetools` (`lib/mice/spad.c`) for
     Amusement IC (S_PAD0) interoperability.
  2. SKPass does NOT claim copyright over `SPAD0_SPN_TABLE` and does NOT
     relicense `desktop/src/SpadTables.h` or any `micetools` material under
     MIT or any other license.
  3. Any redistribution of source code or binaries incorporating
     `desktop/src/SpadTables.h` must retain this attribution and notice in
     full. Prior to commercial or formal open-source relicensing/distribution,
     separate confirmation or permission from the upstream author (Bottersnike)
     should be obtained, or the table must be independently supplied by the
     end user.
  4. THE UPSTREAM DATA AND REFERENCE IMPLEMENTATIONS ARE PROVIDED "AS IS",
     WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
================================================================================
```

---

## Qt 6.8.3 上位机与 MinGW-w64 运行库版权及许可声明

### 1. 上位机打包结构与所含第三方运行库清单

发行包中的单文件上位机 `releases/v1.0/desktop/SKPass-v1.0.exe` 由纯 Win32 启动器（`desktop/src/single_file_launcher.c`）将 `windeployqt` 收集的运行时压缩包（`runtime-v1.0.zip`）作为资源（`RT_RCDATA` ID `101`）封装而成，并通过 `desktop/src/aimeio_resources.qrc` 内嵌了 `x86` / `x64` 两份 `aimeio_skpass.dll`。其内部包含以下第三方二进制与运行时组件：

| 组件类别 | 包含文件 / 模块 | 版权持有人 | 适用开源许可证 |
|---|---|---|---|
| **Qt 6.8.3 核心与模块动态库** | `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll`、`Qt6SerialPort.dll`（及插件按需引入的 `Qt6Svg.dll`、`Qt6Network.dll`） | The Qt Company Ltd. 及其贡献者 | **GNU LGPLv3**（或 GPLv2 / GPLv3；本工程选用 **LGPLv3**，未使用任何仅限 GPL/商业授权的附加模块） |
| **Qt 6.8.3 官方插件** | `platforms/qwindows.dll`、`styles/qmodernwindowsstyle.dll`、`iconengines/qsvgicon.dll`、`imageformats/`（`qico.dll`、`qsvg.dll`、`qjpeg.dll`、`qgif.dll` 等） | The Qt Company Ltd. 及其贡献者 | **GNU LGPLv3** |
| **Qt 内部集成的第三方库** | 静态编译于 `Qt6Core`/`Qt6Gui` 内的 PCRE2、zlib、DoubleConversion、HarfBuzz-NG、FreeType、libpng、libjpeg-turbo 等 | 各对应上游作者 | BSD / MIT / Zlib / FTL 等宽松许可证（详见 Qt 官方 `Third-Party Attributions` 文档） |
| **GCC 运行库（动态与静态）** | 解包目录中的 `libgcc_s_seh-1.dll`、`libstdc++-6.dll`，以及启动器与 `aimeio_skpass.dll` 静态链接的 `-static-libgcc -static-libstdc++` | Free Software Foundation, Inc. | **GPLv3 with GCC Runtime Library Exception v3.1**（GCC 运行库例外条款 3.1） |
| **MinGW-w64 POSIX 线程库** | 解包目录中的 `libwinpthread-1.dll` | mingw-w64 project / Lockless Inc. | **MIT / 3-Clause BSD 许可证**（二进制分发须保留版权与免责声明全文） |
| **MinGW-w64 CRT 运行时** | 静态链接于 EXE / DLL 中的 `libmingw32`、`libmingwex` 及 CRT 启动代码 | mingw-w64 project | Public Domain / Zope Public License (ZPL) 2.1 / MIT / BSD |
| **LLVM-MinGW 运行库（32位 `x86/aimeio_skpass.dll`）** | 由 `i686-w64-mingw32-clang++` 静态链接（`-static-libgcc -static-libstdc++`）至 32 位 `aimeio_skpass.dll` 的 `libc++`、`libc++abi`、`libunwind`、`compiler-rt` | LLVM Project | **Apache License 2.0 WITH LLVM-exception** |
| **内嵌 AimeIO DLL 资源** | `:/aimeio/x64.dll`、`:/aimeio/x86.dll`（`aimeio_skpass.dll`） | 本项目及上游来源 | 含 `desktop/src/SpadTables.h`（`micetools` 表项，见上节声明）及静态链接的 GCC/LLVM/MinGW-w64 运行库 |

### 2. LGPLv3 合规要点与“单文件上位机”动态库替换方法（LGPLv3 §4(d)）

本上位机对 Qt 6.8.3 采用**动态链接（Shared Library）**，并未静态编译 Qt：

1. **动态库解包与用户自行替换机制（满足 LGPLv3 第 4(d)(1) 条）**：
   - 首次运行 `SKPass-v1.0.exe` 时，启动器会将真正的主程序 `SKPass-v1.0-runtime.exe` 与全部 `Qt6*.dll`、插件目录完整解压至 EXE 同级目录下的 `SKPass-v1.0-runtime\payload-<8位十六进制指纹>\` 文件夹中，并写入 `payload.id` 校验标记。
   - 此后再次启动时，只要 `SKPass-v1.0-runtime.exe` 与 `payload.id` 存在且指纹一致，启动器**不会覆盖**该目录中的任何文件。
   - 因此，终端用户可直接进入 `SKPass-v1.0-runtime\payload-<指纹>\` 目录，查看、修改或用自行编译的 ABI 兼容版本替换其中的 `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll`、`Qt6SerialPort.dll` 及 `platforms/`、`styles/` 等插件，随后直接运行该目录下的 `SKPass-v1.0-runtime.exe`（或继续通过外层 `SKPass-v1.0.exe` 启动）。
2. **许可证文本附带要求（LGPLv3 §4(b)）**：
   - 由于 GNU LGPLv3 是基于 GNU GPLv3 的附加许可条款（LGPLv3 §3），正式分发二进制包时，必须在发行包或随附文档中同时提供 **GNU Lesser General Public License v3.0 (LGPLv3)** 与 **GNU General Public License v3.0 (GPLv3)** 的完整文本（见 <https://www.gnu.org/licenses/lgpl-3.0.txt> 与 <https://www.gnu.org/licenses/gpl-3.0.txt>）。
3. **Qt 6.8.3 对应源码获取说明（LGPLv3 §4 援引 GPLv3 §6）**：
   - 本项目分发的 Qt 6.8.3 与 MinGW 13.1.0 运行库均为来自 Qt 官方安装器（`Qt 6.8.3 MinGW 64-bit`）的**未修改官方二进制**。
   - 对应版本的 Qt 6.8.3 完整源码包可自 Qt 官方档案库获取：<https://download.qt.io/archive/qt/6.8/6.8.3/single/>（或通过 Qt 维护工具获取）；若对外进行独立的商业或镜像分发，分发者亦应按 GPLv3 §6 / LGPLv3 §4 的要求确保受让人能够获取所捆绑版本的对应上游源码。

### 3. Qt 6.8.3 与 MinGW-w64 / LLVM 运行库版权及许可声明文本（随发行包保留）

在发布 `SKPass-v1.0.exe` 或包含 `aimeio_skpass.dll` 的二进制发行包时，须保留以下完整声明文本：

```text
================================================================================
Third-Party Notice: Qt 6.8.3 & MinGW-w64 / GCC / LLVM Runtime Libraries
================================================================================

1. Qt 6.8.3 Libraries and Plugins (LGPLv3)
--------------------------------------------------------------------------------
This application (SKPass Desktop) dynamically uses unmodified Qt 6.8.3 libraries
(Qt6Core, Qt6Gui, Qt6Widgets, Qt6SerialPort, and associated platform/style/image
plugins), which are licensed under the GNU Lesser General Public License version 3
(LGPLv3).

  Copyright (C) 2024 The Qt Company Ltd. and other contributors.
  Contact: https://www.qt.io/licensing/

Relinking / Library Replacement Notice (LGPLv3 Section 4(d)(1)):
  The single-file launcher (`SKPass-v1.0.exe`) extracts the dynamically linked
  executable (`SKPass-v1.0-runtime.exe`) and all shared Qt DLLs/plugins into:
    `<exe_dir>\SKPass-v1.0-runtime\payload-<hash>\`
  Subsequent runs do not overwrite this directory while `payload.id` and
  `SKPass-v1.0-runtime.exe` remain intact. Users may freely inspect or replace
  the `Qt6*.dll` shared libraries and plugins in that directory with compatible
  modified builds and execute `SKPass-v1.0-runtime.exe` directly.

Corresponding Source & License Texts:
  - Unmodified Qt 6.8.3 source code is available from:
    https://download.qt.io/archive/qt/6.8/6.8.3/single/
  - Third-party components bundled inside Qt libraries are documented at:
    https://doc.qt.io/qt-6/licenses-used-in-qt.html
  - Full texts of GNU LGPLv3 and GNU GPLv3:
    https://www.gnu.org/licenses/lgpl-3.0.txt
    https://www.gnu.org/licenses/gpl-3.0.txt

2. GCC Runtime Libraries (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, static libgcc/libstdc++)
--------------------------------------------------------------------------------
  Copyright (C) Free Software Foundation, Inc.
  Licensed under the GNU General Public License version 3 with the GCC Runtime
  Library Exception version 3.1 (SPDX: GPL-3.0-only WITH GCC-exception-3.1):
    https://www.gnu.org/licenses/gcc-exception-3.1.html

3. LLVM-MinGW C++ / Unwind / Compiler-RT Runtime (in 32-bit `x86/aimeio_skpass.dll`)
--------------------------------------------------------------------------------
  Copyright (c) 2003-2024 University of Illinois at Urbana-Champaign and the
  LLVM Project contributors.
  Licensed under the Apache License v2.0 with LLVM Exceptions
  (SPDX: Apache-2.0 WITH LLVM-exception):
    https://llvm.org/LICENSE.txt

4. MinGW-w64 `winpthreads` (`libwinpthread-1.dll`)
--------------------------------------------------------------------------------
  Copyright (c) 2011-2016 mingw-w64 project
  Copyright (c) 2010 Lockless Inc.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

5. MinGW-w64 Runtime (`libmingw32`, `libmingwex`, CRT startup)
--------------------------------------------------------------------------------
  Copyright (c) 2009-2023 by the mingw-w64 project.
  The mingw-w64 runtime is mostly in the public domain, with portions covered
  by the Zope Public License (ZPL) 2.1, MIT, and BSD-style licenses.
================================================================================
```

---

## ESP32-S3 预编译固件二进制（`releases/v1.0/firmware/`）版权与许可声明

本仓库源码目录未提交 `esp32/managed_components/`，但 `releases/v1.0/firmware/` 下包含预编译固件镜像（`SKPass.bin`、`first-flash/bootloader.bin`、`first-flash/partition-table.bin`、`first-flash/ota_data_initial.bin`）。根据 `esp32/dependencies.lock` 与 `esp32/main/CMakeLists.txt`，上述固件二进制中链接了以下开源组件，分发固件二进制时须保留其版权与许可声明：

| 固件内含组件 | 锁定版本 / 来源 | 版权持有人 | 适用开源许可证 |
|---|---|---|---|
| **ESP-IDF 框架及驱动**（`driver`、`esp_timer`、`app_update`、`spi_flash`、`esp_app_format`、Bootloader） | v5.2.5 (`espressif/esp-idf`) | Espressif Systems (Shanghai) CO., LTD | **Apache License 2.0** |
| **`espressif/esp_tinyusb`** | v1.7.6~2 | Espressif Systems (Shanghai) CO., LTD | **Apache License 2.0** |
| **TinyUSB 协议栈**（`espressif/tinyusb` / `hathach/tinyusb`） | v0.21.0~2 | Ha Thach (`tinyusb.org`) 及贡献者 | **MIT License** |
| **FreeRTOS 内核** | ESP-IDF v5.2.5 内置 | Amazon.com, Inc. or its affiliates | **MIT License** |
| **cJSON**（ESP-IDF `json` 组件，由 `esp32/main/skpass_cdc.c` 调用） | ESP-IDF v5.2.5 内置 (`DaveGamble/cJSON`) | Dave Gamble and cJSON contributors | **MIT License** |
| **Newlib C 标准库** | ESP-IDF 工具链内置 | Red Hat, Inc. / Regents of the University of California 等 | BSD / MIT 类宽松许可证 |

```text
================================================================================
Third-Party Notice: ESP32-S3 Firmware Binaries (ESP-IDF, TinyUSB, FreeRTOS, cJSON)
================================================================================

1. ESP-IDF Framework & espressif/esp_tinyusb (Apache-2.0)
--------------------------------------------------------------------------------
  Copyright (c) 2015-2025 Espressif Systems (Shanghai) CO., LTD.
  Licensed under the Apache License, Version 2.0 (the "License"); you may not
  use these components except in compliance with the License. You may obtain a
  copy of the License at:
    http://www.apache.org/licenses/LICENSE-2.0

2. TinyUSB Stack (MIT) & FreeRTOS Kernel (MIT) & cJSON (MIT)
--------------------------------------------------------------------------------
  - TinyUSB: Copyright (c) 2018 Ha Thach (tinyusb.org) and contributors
  - FreeRTOS Kernel: Copyright (C) 2021 Amazon.com, Inc. or its affiliates
  - cJSON: Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
================================================================================
```

---

## 其它协议与硬件参考来源版权及合规说明

### 1. `CrazyRedMachine/PN5180-cardio`（GPL-3.0）与 CardIO USB 标识合规说明
- **上游仓库与协议**：<https://github.com/CrazyRedMachine/PN5180-cardio>（其 README 注明基于 `zyp/cardio`），仓库根目录 `LICENSE` 为 **GNU General Public License v3.0 (GPL-3.0)**。
- **本工程实现边界**：本工程 `esp32/components/skpass/SKPass.c/.h` 与 `CardID.c/.h` 未复制 `PN5180-cardio` 的 Arduino/PN5180 C++ 驱动代码或 `CARDIOHID.cpp` 的控制传输实现，而是针对 ESP-IDF + TinyUSB 重新编写的纯 C 报文打包模块；仅保留了用于与宿主软件兼容的 **42 字节 USB HID Report Descriptor**（Usage Page `0xFFCA`、Report ID `1`/`2`）及 **9 字节 HID 输入报告线格式**（含 ISO14443 伪装 FeliCa 时首字节低 4 位掩码 `0x0F`）。
- **合规注意事项**：
  1. 若任何下游修改引入了 `PN5180-cardio` 的受版权保护代码，或将固件作为 `PN5180-cardio` 的 GPLv3 衍生作品分发，则整个固件须遵守 **GPL-3.0** 开源传染条款。
  2. 固件为兼容现有宿主生态而沿用的 USB `VID = 0x1CCF`（USB-IF 注册归属于 Konami Digital Entertainment Co., Ltd.）、`PID = 0x5252` / `0x5253` 及序列号 `CARDIOP1` / `CARDIOP2` 属于互操作性兼容标识，**并非**本项目获 USB-IF 正式授权的商业厂商标识，严禁用于宣称官方认证或商业品牌冒用。

### 2. `ElectronicCats/ElectronicCats-PN7150`（MIT / Beerware）
- **上游仓库与协议**：<https://github.com/ElectronicCats/ElectronicCats-PN7150>，仓库根目录 `LICENSE` 为 **MIT License**（`Copyright (c) 2020 ElectronicCats`）；其示例文件 `examples/MifareClassic_read_block/MifareClassic_read_block.ino` 与 `examples/MifareClassic_write_block/MifareClassic_write_block.ino` 头部标记为 **Beerware**（作者 Salvador Mendoza `@Netxing` / Electronic Cats，无担保声明）。
- **本工程使用方式**：`esp32/main/pn7150.c`、`esp32/main/mifare_reader.c` 与 `esp32/main/card_writer.c` 仅参考其 PN7150 NCI 扩展命令格式（`0x40` 扇区认证、`0x10 0x30` 读取、`0x10 0xA0` 两阶段写入与 `readerReActivate` 重激活时序），基于 ESP-IDF `i2c_master` 接口用 C 语言重写，未复制上游 C++ 类库代码。

### 3. `segatools` AimeIO 接口规范（The Unlicense）与 Microsoft UF2 规范（MIT）
- **`aimeio/aimeio.c/.h`**：导出的 `aime_io_get_api_version`、`aime_io_init`、`aime_io_nfc_poll`、`aime_io_nfc_get_aime_id`、`aime_io_nfc_get_felica_id`、`aime_io_led_set_color` 函数签名及接口约定兼容 `djhackersdev/segatools`（`aimeio` 接口规范，上游采用 **The Unlicense** 公有领域声明）。
- **`esp32/main/uf2_update.c` 与 `esp32/tools/bin_to_uf2.py`**：依据微软公开的 USB Flashing Format (UF2) 规范（<https://github.com/microsoft/uf2>，**MIT License**）独立编写。

---

## SKPass 整理补充

- `esp32/components/skpass/SKPass.c/.h` 与 `CardID.c/.h` 兼容 CardIO/PN5180-cardio 的 42 字节 HID 报表描述符、9 字节报告格式、VID/PID（`0x1CCF:0x5252`/`0x5253`）及序列号（`CARDIOP1`/`CARDIOP2`）；上游 `CrazyRedMachine/PN5180-cardio` 为 **GPL-3.0** 协议，且 `0x1CCF` 为第三方注册 USB VID，公开发布前须注意上述协议边界与 USB 标识使用限制。
- ESP-IDF、esp_tinyusb、TinyUSB 由组件管理器下载，源码目录未包含 `managed_components`；但分发 `releases/v1.0/firmware/` 下的预编译固件二进制时，须保留上方章节所列的 ESP-IDF（Apache-2.0）、TinyUSB（MIT）、FreeRTOS（MIT）与 cJSON（MIT）版权及许可声明。
- 单文件上位机内含 Qt 6.8.3 动态库与插件（LGPLv3）、MinGW-w64/GCC 13.1 运行库（`libgcc`/`libstdc++` 适用 GPLv3 + GCC Runtime Library Exception 3.1，`libwinpthread-1.dll` 适用 MIT/BSD，CRT 适用 Public Domain/ZPL 2.1/MIT）以及通过 `.qrc` 内嵌的 `x86`/`x64` 两份 `aimeio_skpass.dll`（其中 32 位 `x86` 版本静态链接了 `llvm-mingw` 的 `libc++`/`libunwind`/`compiler-rt`，适用 Apache-2.0 WITH LLVM-exception）。正式分发须遵守上方章节所列的 LGPLv3 版权声明、LGPLv3+GPLv3 完整许可文本随附、解包目录动态库可替换说明、Qt 6.8.3 源码获取方式及 MinGW-w64 / LLVM-MinGW 的版权保留要求。现有来源记录不是完整法律授权审查。
- SKPass 原创源码部分由项目所有者采用 **GNU General Public License v3.0 (GPL-3.0)** 开源发布（详见根目录 [`LICENSE`](LICENSE)）；但 [`desktop/src/SpadTables.h`](desktop/src/SpadTables.h) 中的 `micetools` 表项不作为 SKPass 原创作品重新许可，且发行包内随附的 Qt 6.8.3（LGPLv3）、MinGW-w64 / GCC / LLVM 运行库、ESP-IDF（Apache-2.0）、TinyUSB / FreeRTOS / cJSON（MIT）等第三方组件仍分别适用其各自的上游许可证条款。
