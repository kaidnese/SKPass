# SKPass CardIO HID 自检工具

第三阶段测试：`ce_protocol_tests.cpp` 验证卡模拟表单、确认式模式切换、持续日志和最大 459 字节请求的 CDC/HID 回环。固件测试包含 NDEF 文件/写入提交、Listen NCI 长度、信用计数、分片、重新监听与会话失效取消。运行 `ctest --test-dir ../desktop/build -R ce --output-on-failure` 和 `ctest --test-dir ../esp32/tests/build -R "emulation|t4t|session" --output-on-failure`。见 [卡模拟说明与实机验收](../docs/card-emulation.md)。

第二阶段高级读写测试由 `desktop/tests/advanced_ui_tests.cpp` 和 `esp32/tests/advanced_tests.c` 构建：前者验证实际共享协议、CDC 分段/HID 分片与动态表单，后者以模拟 RF 验证认证、地址保护、UID/BCC 与回读。运行 `ctest --test-dir ../desktop/build -R advanced --output-on-failure` 和 `ctest --test-dir ../esp32/build-host-tests -R advanced --output-on-failure`。这些测试不会操作真实卡片；协议与实卡验收见 [高级读写说明](../docs/advanced-rw.md)。

本目录另有 `nfc_protocol_tests.cpp` / `nfc_vectors.c`，由 `desktop/CMakeLists.txt` 构建为 `nfc_protocol_tests`：使用真实固件 JSON 编码器、CDC 分段与 HID 分片重组，再送入 Qt NFC 页面。运行 `ctest --test-dir ../desktop/build -R nfc --output-on-failure` 可验证详情协议，不连接或修改实际卡片。

**这一页讲**：怎么用几行命令确认"读卡器的 HID 通道是好的"。

这是一个 Python 脚本，只做一件事：**读 SKPass 发出的 CardIO 兼容 HID 报文**。它**不碰 CDC、不解析卡背号码、不写卡**。

> HID 里那 8 个字节是给老程序用的兼容标识，**不是 20 位卡号**。要卡号请用 [上位机](../desktop/README.md)，
> 要卡号原理见 [卡号说明](../docs/access-codes.md)。

---

## 1. 能看到什么

* 设备列表（按 `VID:PID = 1CCF:5252`、Usage Page `FFCA` 枚举），含 `CARDIOP1/2` 序列号；过滤掉 v1.5 新增的 `FFCB` 数据 HID。
* 每次刷卡的时间戳、Report ID、8 字节标识、以及完整的原始 HID 报文。
* 可选把同样内容追加写进 CSV。

---

## 2. 安装

需要 Python 3.9+ 和 `hidapi`：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

之后统一用 `.\.venv\Scripts\python.exe` 调用即可（不需要改 PowerShell 执行策略）。标准 HID 设备一般不需要装厂商驱动。

---

## 3. 用法

```powershell
# 列出设备，索引从 0 开始
.\.venv\Scripts\python.exe skpass_host.py --list

# 监听第一台设备，Ctrl+C 退出
.\.venv\Scripts\python.exe skpass_host.py

# 监听索引 1 的设备，并追加保存 CSV
.\.venv\Scripts\python.exe skpass_host.py --device 1 --csv card_log.csv
```

| 参数 | 默认 | 说明 |
|---|---|---|
| `--list` | — | 只列设备后退出 |
| `--device N` | `0` | 设备索引 |
| `--csv <路径>` | **不写 CSV** | 只有显式给出这个参数才会写文件；文件为空时会写表头 |
| `--timeout ms` | `1000` | HID 读超时 |

退出码：`0` 正常，`1` 没找到设备，`2` 设备索引越界、命令行参数错误或缺少 `hidapi` 依赖。

输出示例与 CSV 列：

```text
2026-10-11T12:00:00.123 CARD report_id=2 UID=02 34 56 78 00 00 00 00 RAW=02 02 34 56 78 00 00 00 00
CSV: timestamp, report_id, card_id, raw_report
```

`UID` 是沿用的列名，实际是固件整理过的 HID 标识，**不一定等于完整物理 UID**。分享 CSV 前请自行脱敏。

---

## 4. 报文格式

正常一次刷卡，主机收到 **9 字节**：

| 字节 | 含义 |
|---|---|
| 0 | Report ID：`1` = ISO15693，`2` = FeliCa / NFC-A / NFC-B |
| 1–8 | 8 字节兼容标识 |

脚本对两种返回约定都做兼容：先按 9 字节解析（首字节是 Report ID），再退到 8 字节分支（`data[1:8]` 右侧补零）。**两个分支都要求首字节是 `1` 或 `2`**，否则打印 `DEBUG 无法解析: ...` 并跳过。

数据怎么整理的（固件侧）：

* FeliCa：IDm 原样 8 字节；
* NFC-A / NFC-B：截断或补零到 8 字节，**首字节 `&= 0x0F`**；
* ISO15693：字节序已归一化。

所以 **Report ID 2 无法区分 FeliCa、NFC-A、NFC-B** —— 想知道准确的 RF 类型，看上位机的数据 HID / 兼容 CDC 诊断。

另外：

* **3 秒的上报节流是固件行为**（`SKPASS_SUPPRESS_MS`），不是本工具做的；本工具没有任何节流逻辑。
* 本工具读取的 **CardIO HID** 不含扇区数据、S_PAD0、也不含精确的移卡通知。要看这些请用上位机；v1.5 默认自动连接另一份数据 HID。

---

## 5. 出问题时

| 现象 | 先查 |
|---|---|
| `--list` 找不到设备 | 固件是否已烧录、是否插在 **S3 原生 USB 口**、USB 线是否支持数据、设备管理器里有没有这个 HID |
| 出现了一个磁盘，但没有 HID | 设备在 UF2 更新模式（PID `0x5253`）。完整断电重启后再试 |
| 能枚举但打不开 | 有程序独占了该 HID 接口；另外确认 `hidapi` 装在当前虚拟环境里 |
| 有报文但"卡号"不对 | 这里本来就只有 8 字节兼容标识。认证与完整读块结果要去上位机看 |

游戏 AimeIO v1.5 占用的是独立 **数据 HID（FFCB）**，旧固件回退 CDC；本工具读的是 **CardIO HID（FFCA）**。接口不同，不需要为本工具改游戏配置。数据 HID 详情见 [USB HID 说明](../docs/usb-hid.md)。
