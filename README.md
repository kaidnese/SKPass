# SKPass

**SKPass 是一款专为街机游戏打造的 ESP32-S3 + PN7150 USB 读卡器 —— 刷一下游戏卡，电脑就能直接解析出物理 ID 与 Aime卡背 20 位 Access Code；原生兼容 CardIO 与 AimeIO（segatools）**

当前组件版本：上位机 **v1.0**、固件 **1.0.0**、AimeIO 接口 **1.0**（统一发行版本 v1.0；固件使用三段式 1.0.0）。

## 硬件部分
详见 [立创开源硬件工程](https://oshwhub.com/kaidnses/project_rqfspswk)

**注意：由于缺乏专业射频测试设备，无法保证天线最佳匹配性能。实测读取官方 Aime 卡在空气中约有 5cm 感应距离，但若是兼容卡 / Classic 卡则需要贴近天线才能读取，且有一定概率读取失败。**
## 这是什么？

一款由 **ESP32-S3** 和 **PN7150** 组成的多功能 USB NFC 刷卡器与开发工作台：

- **刷卡与写卡功能**：
  - **双协议兼容**：同时原生支持 **CardIO HID**（`1CCF:5252` 标准街机读卡器协议）与 **AimeIO**（基于免驱数据 HID 或兼容 CDC），即插即用，无需繁杂的虚拟串口配置。
  - **游戏生态支持**：可无缝刷入集成了 segatools 或支持 CardIO 的各类游戏与街机模拟环境。
  - **卡背号离线解析**：不仅能读取卡片 UID/IDm，还可直接解析 AiMe / Amusement IC 卡背的 **20 位 Access Code** 号码（支持 MIFARE Classic 扇区数据认证读取，以及 FeliCa `S_PAD0` 密码置换离线本地解密，无需抓包与联网）。
  - **兼容卡写号制卡**：支持将 20 位号码安全写入空白/自有 MIFARE Classic 1K 兼容卡（仅限写入 Block 1 与 Block 2，保留厂商信息与密钥区，双重逐字节回读校验）。

- **游戏辅助与 AimeIO 生态扩展**：
  - **自定义虚拟卡与按键刷入**：内置虚拟卡管理系统，支持录入多张卡号并备注；配合项目提供的 32 位 / 64 位 `aimeio_skpass.dll`，在游戏内按住自定义物理按键（如 Enter）即可实现虚拟呈卡，松开即移卡。
  - **segatools 一键配置与备份**：上位机内置 segatools 编辑器，支持自动定位游戏目录、一键部署对应位数的 DLL 并写入 `segatools.ini`；支持自定义 AimeIO DLL 文件名，修改前自动对原 INI、DLL 与卡片配置进行带时间戳的备份，支持一键还原，并完整保留原始 INI 的注释与编码。

- **日常 NFC 与交通卡信息读取**：
  - 支持读取全国交通联合卡、城市公交卡、香港八达通及银联 PBOC 闪付卡的公开信息，脱敏呈现公开卡号、有效期、交易状态，并读取卡内余额或估算历史余额。

- **分卡型高级安全读写（高级 NFC 工具箱）**：
  - **严格在场身份绑定**：所有高级操作均在硬件层面绑定短期动态租约（Token + UID + 协议 + 容量），卡片离场、更换或断线立即安全阻断，杜绝串写；读取结果在独立表格中持久保留，不因移卡丢失，支持一键复制与导出 CSV。
  - **1. MIFARE Classic**：支持按指定 Key A / Key B 认证读写指定块；支持扇区尾块写入安全解锁（需二次确认并校验访问位反码）；支持全安全区只读扫描（Classic 4K 覆盖全部 40 个扇区）与未读/失败项断点重试；支持自行确认的 Gen2 CUID Block 0 UID 改写（固件自动重算 BCC）。
  - **2. Type 2 Tag（NTAG / Ultralight）**：限制安全用户页范围（Page 4 起），支持读取指定页区间（`0x30` 每次读 4 页窗口）与单页写入（`0xA2` 每次写 4 字节）；支持一键格式化写入标准 NDEF 网页链接（URL）或纯文本记录。
  - **3. Type 3 Tag（FeliCa Lite-S）**：严格限制仅针对可安全访问的 Scratchpad 暂存块（`00–0D`）执行单块 16 字节免密读取（Service `000B`）与写入（Service `0009`），绝不触碰 `0E` 之后的 MAC 与系统配置区。
  - **4. Type 5 Tag（ISO15693 / ICODE）**：根据 `Get System Information (0x2B)` 动态探测卡片实际用户块数与 4 字节块大小，按 UID 读写指定用户数据块并回读校验。

- **NFC 卡模拟与固件维护**：
  - **NFC-A 卡模拟（Type 4 NDEF / APDU）**：可无缝切换为 NFC-A ISO-DEP Type 4 虚拟标签，支持自定义 4 字节 UID、模拟 NDEF 载荷（URL 网址 / UTF-8 文本）、捕获外部写入事件，并提供最多 4 组自定义 APDU 交互应答规则，模式切换无需重启芯片。
  - **免拆固件升级**：支持 UF2 MSC 虚拟 U 盘拖拽升级，并内置基于原生 USB / UART0 的 ROM 串口烧录工具，无需安装外部 Python 环境即可完成固件升级与首次部署。


## 文件结构

项目包含固件、上位机、游戏 DLL、测试工具、技术文档和发行文件。下面列出各文件夹的内容与用途。

```text
SKPass/
├─ esp32/                    ESP32-S3 + PN7150 固件源码、配置与测试
├─ desktop/                  Qt Windows 上位机源码和界面资源
├─ aimeio/                   游戏端 AimeIO DLL 源码，支持 x86 / x64
├─ shared/                   上位机与 AimeIO 共用的 Windows HID 通讯层
├─ host-test/                Python CardIO USB HID 测试工具
├─ docs/                     详细实现、协议、排障与验证记录
├─ scripts/                  构建、打包和固件导出脚本
└─ releases/                 编译好的发行文件
   └─ v1.0/                  唯一发行版
      ├─ firmware/           SKPass.bin 应用固件
      │  └─ first-flash/     首次烧录所需的引导程序、分区表和 OTA 初始数据
      ├─ desktop/            SKPass-v1.0.exe 上位机，内置 esptool
      ├─ aimeio/
      │  ├─ x86/             32 位 aimeio_skpass.dll
      │  └─ x64/             64 位 aimeio_skpass.dll
      ├─ SHA256SUMS.txt      发布二进制校验和
      ├─ VERIFICATION.md     发行构建验证记录
      └─ LICENSE.*           捆绑组件许可证文本
```

| 文件夹 | 放的是什么 | 什么时候看 |
| --- | --- | --- |
| [esp32](esp32/README.md) | ESP-IDF 工程、PN7150 驱动、CardIO HID、数据 HID/CDC、读写卡和 UF2 更新实现 | 接线、编译或修改读卡器固件 |
| [desktop](desktop/README.md) | Qt 上位机源码、界面资源、卡号解析、虚拟卡管理、segatools 编辑和更新界面 | 编译或修改上位机 |
| [aimeio](aimeio/README.md) | 游戏端 DLL 源码，通过数据 HID（旧固件回退 CDC）读取实体卡，通过 JSON 和按键呈现虚拟卡 | 配置游戏刷卡或编译 x86/x64 DLL |
| [shared](shared/README.md) | Windows HID 枚举、独占连接、异步读写与会话标识；不依赖第三方 HIDAPI DLL | 修改或排查电脑端 USB 通讯 |
| [host-test](host-test/README.md) | Python HID 监视器与依赖清单，只检查 CardIO 标识报文 | 检查 USB HID 是否正常 |
| [docs](docs/esp32-implementation.md) | 固件详细实现、卡号解析、写卡/更新协议、历史排障与验证说明 | 深入了解实现或定位问题 |
| scripts | PowerShell 脚本：构建固件、上位机与两种位数的 DLL，打包 EXE、导出固件 | 从源码生成发行文件 |
| [releases](releases/v1.0/README.md) | 按版本保存的 BIN、EXE、两份 DLL、首次烧录辅助文件及校验清单 | 直接使用已编译的软件和固件 |

**直接使用请看 `releases/v1.0/`；开发固件进入 `esp32/`，开发上位机进入 `desktop/`，游戏配置与 DLL 说明看 `aimeio/`。**

---

## 1. 它能做什么

| 你想做的事 | 用哪个入口 |
|---|---|
| 刷卡看号码（并对卡背核对） | 上位机「读卡概览」 |
| 让支持segatools的游戏认这张卡 | AimeIO DLL（放游戏目录，游戏运行时不用开上位机） |
| 给兼容 CardIO 的程序读卡 | 接入电脑即可，免驱支持热插拔 |
| 把号码写进自己的空白卡 | 上位机「写卡与计算」（只支持 MIFARE Classic 1K） |
| 在segatools支持不拿实体卡也能刷卡 | 上位机「虚拟卡管理」（默认为Enter刷卡） |
| 升级或恢复固件 | 上位机「固件更新」（UF2 磁盘 / ROM 串口下载） |
| 读取交通/银行公开信息与存储卡容量 | 上位机「NFC」（只读，交通卡统一分类） |
| 模拟 URL / 文本标签、观察 APDU 与配置应答 | 上位机「NFC → 卡模拟」|

---

## 2. 三分钟上手

1. **接线烧固件**：ESP32-S3 接 PN7150（SDA=GPIO4、SCL=GPIO5、IRQ=GPIO7、VEN=GPIO6、I²C 地址 `0x28`），用 USB 接 **S3 原生 USB 口**（不是板上的 USB-UART 调试口）。首次烧录用 `esp32/README.md` 或发行包里的 esptool 命令（4 个文件：bootloader / 分区表 / ota_data_initial / 应用）。
2. **跑上位机**：直接运行 `releases/v1.0/desktop/SKPass-v1.0.exe`（免安装）。
3. **连接**：默认自动连接 SKPass 数据 HID，等到绿色「读卡器已连接」。若已手动断开，选择「自动连接 SKPass USB（优先 HID）」再点「连接」。每秒收到一次心跳，固件标识形如 `SKPass-1.0.0-retry-rom-adv-scan-ce-uf2-write`；只有旧固件没有数据 HID 时才回退串口。
4. **刷卡**：把卡放到 PN7150 天线上，看「读卡概览」里的号码、UID/IDm、**来源**和失败原因。
5. **给游戏用**：见[第 6 节](#6-给游戏用aimeio)。

> 心跳只代表 USB 通道通。**心跳正常 ≠ 读卡成功** —— 号码能不能拿到，取决于卡本身（见下一节）。

---

## 3. 怎么拿到 AiMe 卡背那串 20 位号码

### 3.1 先分清三个完全不同的"号"

| 名称 | 是什么 | 和卡背号码的关系 |
|---|---|---|
| UID / IDm | 卡的物理标识（NFC-A 的 UID、FeliCa 的 8 字节 IDm） | **没有关系**。把 UID 转成十进制也不是卡号 |
| CardIO 8 字节标识 | 给兼容 CardIO 的游戏用的 8 字节（HID 报告里那 8 个字节） | **没有关系**，格式是历史约定 |
| **Access code** | **卡背印的 20 位十进制号码** | 就是它 |

看懂这一条，就能避开 90% 的困惑：**读到 UID 不代表读到了卡号。**

### 3.2 卡背号码从哪来 —— 看你的卡属于哪一类

#### 情况 A：Classic AiMe 卡（MIFARE Classic 1K，闲鱼兼容卡大多是这类）

卡背号码**就写在卡里**，流程是：

```
固件读 Sector 0 的 Block 0 / 1 / 2（共 48 字节）
        ↓
号码在 Block 2 的偏移 6，共 10 字节（BCD：每字节两位十进制数字）
        ↓
上位机把它变成 20 位数字，来源标为「来自卡内数据」
```

Block 2 的布局（`C0…C9` 就是号码的 10 个 BCD 字节）：

```text
Block 2: 00 00 00 00 00 00 C0 C1 C2 C3 C4 C5 C6 C7 C8 C9
                            └────── 20 位号码 ──────┘
```

读这一块需要**认证**（MIFARE Classic 的扇区密钥）：

* 固件默认用 Key A = `574343467632`（ASCII 是 `WCCFv2`，Kconfig 里 `SKPASS_MIFARE_KEY_A` 可改）；
* 认证不成功时，固件会再试一次空白卡出厂密钥 `FFFFFFFFFFFF`；
* 认证成功后才读块。**读不到块 ≠ 卡是空白卡**，也可能只是密钥不对、访问控制不允许或天线耦合差。

拿到号码后**请和卡背比对一次**：上位机显示的是"从卡里读出来的数字"，不是官方数据库的结论。

#### 情况 B：Amusement IC / FeliCa 卡（卡背有 Amusement IC 标记）

这类卡的号码**不在明文区**，而是加密存在卡里的 **S_PAD0**（16 字节）：

```
固件用 FeliCa「Read Without Encryption」只读命令读出 S_PAD0 的 16 字节密文
        ↓
上位机本地解密（替换密码，9 张 256 字节置换表；表来自 micetools 的公开实现）
        ↓
检查版本字节低半字节为 0 → 取明文偏移 6..15 的 10 字节 BCD → 20 位号码
        ↓
来源标为「来自 S_PAD0」
```

* 解密**不需要密钥**：16 字节密文本身包含全部参数，任何人拿到都能离线解开。
* **但能不能读到那 16 字节，取决于卡**：有些卡的 S_PAD 区被配置为"必须认证后才能读"，此时读出失败，状态码 `0x11`。
* 固件会依次探测 4 种服务码写法（`0B 00` / `00 0B` / `09 00` / `00 09`）与 ID 块 `0x82`、S_PAD0 块 `0x00`；打开 `SKPASS_FELICA_FULL_SCAN` 还会全量只读扫描并把每次结果打进日志，方便判断到底是"卡不允许"还是"服务码不对"。
* 顺带能读到**发行方**：ID 块 `0x82` 的字节 8~9 是 DFC（`0078` SEGA、`0068` Konami、`002A`/`003A` Bandai Namco、`0079` Taito、`8CA2` 旧 Banapass）。

#### 情况 C：确实拿不到印号的情况

| 退路 | 说明 | 注意 |
|---|---|---|
| 手输卡背号码 | 直接输入/粘贴 20 位（空格、横线、全角数字会自动规整） | 仅能在虚拟卡使用 |
| 用 8 位 Serial 计算 | 只对 `01036…` 体系的 Classic 号码有效：`01036` + 牌堆加密(8 位) + 7 位摘要 | **Serial 不是 UID**，不能拿 UID 的十进制来填 |
| 派生号 | 把 IDm 当大端有符号整数取绝对值补到 20 位 | **不是卡背印号**，界面会明确标注，且**不会记入「最近刷入卡号」历史表**（防止误存为虚拟卡）；仅供服务器按 IDm 认卡时对齐 |

另外还有「随机生成」：只用于给**自己的空白卡**制卡，不保证唯一，更不等于官方注册。

### 3.3 各类卡能得到什么

| 卡型 | 能读到 | 能拿到 20 位印号吗 | 能写吗 |
|---|---|---|---|
| MIFARE Classic 1K（含 Classic AiMe） | UID、SAK、Sector 0 的 Block 0/1/2 | **能**（认证通过时，来自 Block 2 偏移 6） | **能**，只写 Block 1/2 |
| FeliCa / 适用的 Amusement IC | IDm、PMm、可读时的 S_PAD0 | **能**（S_PAD0 可读且格式符合时，本地解密） | 不能 |
| 旧 Banapass / 其他 FeliCa | IDm、PMm | 没有通用算法，取决于卡内格式 | 不能 |
| 其他 NFC-A / NFC-B | UID / PUPI | 不能 | 不能 |
| ISO15693 | UID（仅 CardIO HID 标识） | 不能 | 不能 |

当前默认轮询 NFC-A、NFC-B、NFC-F 和 ISO15693。细节与代码依据见 [卡型与 access code](docs/access-codes.md)。

---

## 4. 读不到号码时的排查

| 现象 | 原因 / 怎么办 |
|---|---|
| 心跳正常，但一张卡都读不到 | PN7150 初始化或焊接问题。看日志是否有 `NFC_RECOVERY`；检查供电、天线距离、卡是否贴对位置 ， **自制cuid卡可能要取下外壳才能使用** |
| 有 UID，但显示"未取得 access code"，状态 `0x12` | Classic 认证失败：换 `SKPASS_MIFARE_KEY_A`（空白卡是 `FFFFFFFFFFFF`）、把卡放正、重新刷一次。**不能据此判断卡是空白卡** |
| 状态 `0x13` | 读块/会话失败（含重选失败）。固件已做 3 次有界重试（每次重选+重新认证），仍失败通常是耦合/供电问题 |
| 状态 `0x11`（FeliCa） | 没读到 S_PAD0：该卡可能要求认证后才能读。打开 `SKPASS_FELICA_FULL_SCAN` 看日志里各服务码/块的状态码（`0xA7` 表示该块访问不允许） |
| 同一张卡 3 秒内不再上报 | 这是 `SKPASS_SUPPRESS_MS`（默认 3000 ms）的全局节流，不是故障；也**不需要**先刷另一张卡 |
| 日志里看到 `raw=100400` / `10B0` 且 `ESP_OK` | 这只是收到了响应，**不代表读块成功**，要看后面的状态字节 |
| 游戏读不到卡 | ①上位机是否已「断开」（游戏 DLL 与上位机独占同一个数据 HID，旧固件使用 CDC）；②DLL 位数是否与游戏进程一致（x86/x64 手动选）；③`segatools.ini` 的 `[aimeio] path` / `cardsPath`、`[aime] enable` 是否写入；④虚拟刷卡时 `cards.json` 是否**启用**了至少一张卡 |
| 写卡按钮是灰的 | 需要 RW 模式、固件标识以 `-write` 结尾**且**已读到 `sak=08`、`MIFARE` 接口的 Classic 1K 卡 |
| 「进入 UF2 更新」是灰的 | 需要有效连接且固件标识包含 `-uf2-` |
| 「进入 ROM 下载」是灰的 | 软件进入需要包含 `-rom-` 能力标识的固件（当前 1.0.0 固件原生支持）和正常连接；旧固件可先 UF2 升级，也可 BOOT + RESET 手动下载 |
| 更新完成但不确定成功 | 必须收到磁盘 `STATUS.TXT` 的 `COMPLETE`；支持自动重启的更新器（v1.0 固件已内置）会在写入成功后自动重启，重连后按需查询版本；未包含自动重启特性的旧更新器需按复位键或断开全部供电重插 |

---

## 5. 写卡与计算（只针对自己的空白/可改写 Classic 1K）

只写 **MIFARE Classic 1K**，且**只写两个块**：

| 块 | 写入内容 |
|---|---|
| Block 0 | **不写**（保留 UID 和厂家信息） |
| Block 1 | 16 字节 `00` |
| Block 2 | 6 字节 `00` + 10 字节 BCD 号码 |
| Block 3 | **不写**（保留密钥和访问控制） |

流程：放入目标卡 → 上位机记录完整 UID → 输入 20 位号码（或由 Serial 计算/随机生成）→ 选择**卡当前的 Key A**（默认 `FFFFFFFFFFFF`，WCCFv2 卡选 `574343467632`，也可自定义 12 位十六进制）→ 确认弹窗 → 等结果（固件依次执行：**认证 → 预读 Block 1/2 → 两阶段写入 Block 1/2 → 逐字节回读校验**）。

必须知道的三件事：

1. **只有"写入 + 固件与上位机双重逐字节回读校验通过"才算成功**；固件待写队列有效期 **15 秒**（CDC 掉线或 HID 会话关闭/过期后作废），上位机等待上限 **20 秒**，超时或断线一律按"结果未知"处理，请重新读卡核对。
2. **不会自动重发、不会回滚**：MIFARE 没有跨块事务，中途移卡或掉电可能只写完一个块。
3. **写号码不会改密钥**：给 FF 空白卡写号后，密钥仍然是 FF，并不会变成 WCCFv2；不改 UID、不改访问控制，也不代表官方发卡或已在服务器注册。

---
<span id="6-给游戏用aimeio"></span>
## 6. 给游戏用（AimeIO）

1. 用上位机「segatools 编辑」：选 `segatools.ini` 所在目录 → 选 **游戏/加载进程的位数**（x86 或 x64，必须与进程一致，不会自动判断）→ 预览 → 「放入 DLL · 备份并应用」。
2. 它只改三项，并会先备份 INI/DLL/JSON：

```ini
[aimeio]
path=aimeio_skpass.dll
cardsPath=cards.json

[aime]
enable=1
```

3. 刷实体卡给游戏时：数据通过 HID 给 DLL。上位机不用关，可以继续管虚拟卡。游戏目录下要有aimeio_skpass.dll,cards.json。
4. 虚拟卡刷卡：在「虚拟卡管理」保存并**启用**一张卡，设置刷卡键（默认 Enter），游戏里按住键 = 呈卡，松开 = 移卡。

AimeIO 的呈卡规则（代码实际行为）：

* 实体卡**优先于**虚拟卡；
* Classic 卡：把 10 字节号码走 `aime_io_nfc_get_aime_id`；
* FeliCa 卡：S_PAD0 解密成功就把号码走同一个 Aime ID 接口；失败则回退原始 IDm（走 `aime_io_nfc_get_felica_id`，DLL **不做**任何 UID→号码推算）；
* 卡的有效期 5 秒：5 秒内没有新数据即视为无卡；
* 虚拟卡只支持 Classic 那种 20 位号码形式（不保存虚拟 FeliCa 身份）。

---

## 7. 固件更新（UF2 / ROM）

更新页有两个标签：日常使用 **UF2 磁盘更新**；需要串口下载或恢复设备时选择 **ROM 串口下载**，支持原生 USB 和外接 UART0（GPIO43 TXD0 / GPIO44 RXD0，3.3V 电平）。ROM 页选择文件后还需确认烧录，普通更新与首次烧录的覆盖范围不同；详见 [更新说明](docs/firmware-update.md)。

* 上位机「固件更新」里选择或拖入 **应用 BIN** 或 **SKPass 兼容 UF2**；上位机会把合法 BIN 转成 UF2。
* 点「进入 UF2 MSC 更新模式」后，设备变成一块 U 盘（卷标 `SKPASS_UF2`，PID 变为 `0x5253`），读卡功能停止。
* 电脑复制完**不算完**：要等磁盘里的 `STATUS.TXT` 显示 `COMPLETE`；`ERROR:` 则保留旧固件。
* 写入的是**非活动 OTA 槽**，且**没有**健康检查自动回滚。当前固件更新器（v1.0 固件已内置）在电脑读到 `COMPLETE` 后约 2 秒自动重启，上位机自动重连；无人读取状态时约 17 秒后重启。重连后用「获取设备信息」核对版本。
* 自动重启由**当前设备正在运行的更新器**执行：第一次从早期旧版本更新到支持自动重启的版本时，仍需手动复位一次；装好后再更新即可全自动重启。校验失败不自动重启。
* 只接受本工程格式的 UF2（family `0xC47E5767`）；**不要**拖 bootloader、分区表或整片 Flash 镜像。
* 从旧的 `pn7150_cardio` 迁移：必须**完整烧录一次**（bootloader + 分区表 + ota_data + 应用），旧 UF2 更新器不接受改名后的镜像。

---

## 8. 专项文档导航

| 想了解 | 看这里 |
|---|---|
| 卡号从哪来、发行方怎么判断、为什么和游戏显示不一致 | [卡型与 access code](docs/access-codes.md) |
| 接线、编译、烧录、Kconfig、USB 协议 | [固件说明](esp32/README.md) |
| 上位机各页面怎么用 | [上位机说明](desktop/README.md) |
| DLL、INI、按键刷卡 | [AimeIO 说明](aimeio/README.md) |
| HID 自检 | [host-test](host-test/README.md) |
| 自动连接、HID/CDC 回退、USB 会话与协议 | [USB HID 说明](docs/usb-hid.md) |
| 预编译产物怎么用 | [发行说明](releases/v1.0/README.md) |
| Type 4 标签模拟、UID 与 APDU 应答 | [卡模拟](docs/card-emulation.md) |
| 逐条协议、重试、恢复、排障细节 | [固件实现说明](docs/esp32-implementation.md) |
| 写卡协议 | [写卡](docs/writing.md) |
| Classic 读取重试设计 | [读取重试](docs/mifare-retry.md) |

---

## 9. 构建

| 部分 | 工具链 | 产物 |
|---|---|---|
| 固件 | ESP-IDF **5.2.5** → `esp32/` | `esp32/build/SKPass.bin` |
| AimeIO | Windows C++17，x86 与 x64 各一份 | `aimeio/build-<arch>/aimeio_skpass.dll` |
| 上位机 | **Qt 6.8.3 MinGW x64** + CMake ≥3.21 + Ninja | `desktop/build/SKPass.exe`，打包后为单文件 EXE |

```powershell
# 固件（在 esp32/ 里）
idf.py build

# AimeIO（在仓库根目录）
./scripts/build-aimeio.ps1 -Architecture x64 -CompilerBin <工具链 bin>
./scripts/build-aimeio.ps1 -Architecture x86 -CompilerBin <工具链 bin>

# 上位机（先有上面两份 DLL，再构建）
./scripts/build-desktop.ps1 -QtPrefix <Qt>/6.8.3/mingw_64 -CompilerBin <Qt>/Tools/mingw1310_64/bin
./scripts/package-desktop.ps1 -QtPrefix <Qt>/6.8.3/mingw_64 -CompilerBin <Qt>/Tools/mingw1310_64/bin
```

单文件 EXE 首次运行会把运行库解包到 **EXE 同目录的 `SKPass-v1.0-runtime\payload-<指纹>\`**（因此不能放在只读目录）。ROM 下载器的临时文件与自解包也在该运行目录内，不使用 C 盘默认 TEMP。偏好设置在注册表 `HKCU\Software\SKPass\SKPass v1.0`；首次正常启动可自动继承此前开发版的 JSON/游戏目录偏好设置，`cards.json` 未配置时默认在 EXE 旁。

在 VS Code 里打开 `SKPass.code-workspace` 时，ESP-IDF 工程目录要选 **`esp32`**，不要对仓库根目录跑 IDF 构建。

---

## 10. 现在验证到什么程度

已经通过的：

* 已经确认蓝白卡可以刷入且access code正确
* Aimeio.dll 仅确认中二和maimai可用，其他支持segatools的游戏并没有做测试
* cardio 仅在SDVX验证可用，并没有验证其他游戏。
* 高级读写新增模拟 RF、身份绑定、保护边界、CDC/HID 队列及 Qt 动态表单测试；完整数量和范围见 [验证记录](docs/verification.md)。
* 发行包 SHA256 见 `releases/v1.0/SHA256SUMS.txt`。

**还没有做**：
* 目前只验证了蓝白aime卡的读取， 旧 Banapass / 其他 FeliCa 并没有实测
* 电池供电部分和无线连接并没有做
* 只对自制的cuid卡与网上买到的cuid卡进行了读写验证。对自制pcb的cuid卡读写距离有限，必须贴近才能读写，uid倒是很容易读出来）

* **不联网**：没有 AiMeDB / TCP 22345 查询，没有账号注册 —— 号码全部本地解析。
* FeliCa 只开放已验证 Lite-S `88B4` 的 Scratchpad `00–0D` 免密读写；不做 FeliCa 认证或配置区写入。
* 仅允许已确认卡型范围内的高级块/页操作，不提供任意 NFC 指令接口。
* 写卡无自动重试、无回滚；UF2 更新无自动回滚。
* 数据 HID 不支持上位机和 AimeIO 同时连接；AimeIO 的 `aime_io_nfc_get_mifare_block` 不提供原始扇区块（参数合法时恒返回 `S_FALSE`，参数非法返回 `E_INVALIDARG`）。

---

## 11. 开源许可、第三方声明与免责说明

### 11.1 开源许可证（GPL-3.0）与第三方组件边界
* **项目原创代码**：除 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) 所列的第三方数据表与运行库外，SKPass 的原创源码（含 `esp32/`、`desktop/`、`aimeio/`、`shared/`、`host-test/`、`scripts/`）采用 **[GNU General Public License v3.0 (GPL-3.0)](LICENSE)** 协议开源发布。任何基于本项目源码的修改或二进制再分发均须遵守 GPL-3.0 协议并公开对应源码。
* **第三方表项与运行库例外**：
  * [`desktop/src/SpadTables.h`](desktop/src/SpadTables.h) 中的 2304 字节 `SPAD0_SPN_TABLE` 表项提取自 `Bottersnike/micetools`（详见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)），本项目不对该表主张原创版权，亦不将其重新许可为 GPL-3.0 或其它协议（注：ESP32-S3 固件本身不包含该表）。
  * 发行包内捆绑的未修改 **Qt 6.8.3** 动态库与插件适用 **GNU LGPLv3**（解包至同级 `SKPass-v1.0-runtime\payload-<指纹>\` 后，用户可检视或替换 `Qt6*.dll`）。ROM 工具 esptool 4.12.0 适用 GPL-2.0-or-later；MinGW-w64 / GCC / LLVM 运行库及固件内 ESP-IDF（Apache-2.0）、TinyUSB（MIT）、FreeRTOS（MIT）、cJSON（MIT）适用各自上游许可，完整声明见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

### 11.2 技术分享免责说明
* **仅限技术研究与个人学习交流**：本项目旨在分享 ESP32-S3 + PN7150 NFC 读写与 USB 复合设备开发技术。项目软件与固件完全免费开源，不收取任何软件授权费用；
* **非官方外设与商标/标识声明**：本项目与 SEGA、Konami、Bandai Namco、Taito 或 Sony（FeliCa）等公司无任何关联、赞助或授权关系。固件中为兼容现有社区宿主软件而保留的 CardIO 协议格式及 USB `VID:PID`（`1CCF:5252` / `1CCF:5253`）仅作本地互操作性测试用途，不得作为商业认证外设或冒用任何第三方品牌销售。
* **按“原样（AS IS）”提供**：本项目软硬件均不提供任何明示或暗示的担保，使用者需自行承担刷写固件、读写卡片及硬件操作的一切风险。


