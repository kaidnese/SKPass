# SKPass AimeIO（游戏端 DLL）

**这一页讲**：把这个 DLL 放进游戏目录后，实体卡和虚拟卡是怎么被游戏看到的、INI/JSON 怎么写、刷不上卡时查什么。

它是 **Windows AimeIO API 1.0** 的实现（`aime_io_get_api_version()` 恒返回 `0x0100`），供支持自定义 AimeIO 的 segatools 使用。**游戏运行时不需要打开上位机** —— DLL 自己连接数据 HID（旧固件回退 CDC）或读取 `cards.json`。当前 v1.0 DLL 配套固件 1.0.0；不需要为 ROM 下载更新游戏 DLL。

---

## 1. 先选对 DLL（这一步最容易错）

| 加载 DLL 的进程 | 用哪个文件 |
|---|---|
| 32 位 | [x86/aimeio_skpass.dll](../releases/v1.0/aimeio/x86/aimeio_skpass.dll) |
| 64 位 | [x64/aimeio_skpass.dll](../releases/v1.0/aimeio/x64/aimeio_skpass.dll) |

* **按"加载 DLL 的进程"位数选，不是按 Windows 位数选。** 游戏画面进程和真正加载 AimeIO 的进程可能不是一个。
* DLL **不会**自动判断位数，也没有自动切换。
* DLL 可以改名（它靠自己的模块路径定位目录，不校验文件名），但 INI 里的 `path` 必须与实际文件名一致。
* 上位机「segatools 编辑」里内置了这两份 DLL，选好位数和名称后会自动复制过去并备份。

---

## 2. 配置

关掉游戏，把 DLL 放到 `segatools.ini` 所在目录，然后：

```ini
[aimeio]
path=aimeio_skpass.dll
cardsPath=cards.json

[aime]
enable=1
```

哪些键是谁读的，别搞混：

| 键 | 谁读 | 说明 |
|---|---|---|
| `[aimeio] path` | **segatools 的自定义 AimeIO 加载器** | DLL **自己不读**这个键 |
| `[aime] enable` | **segatools** | DLL 自己不读 |
| `[aimeio] cardsPath` | **本 DLL** | DLL 唯一读取的 INI 键 |

`cardsPath` 的解析规则（DLL 代码行为）：

1. 先读 **DLL 所在目录**的 `segatools.ini`；
2. 只有取值仍为空时，才读**当前工作目录**的 `segatools.ini`；
3. 取值是绝对路径就用它；否则以 **DLL 所在目录** 为基准拼接；
4. 完全没配置 → 用 **DLL 所在目录的 `cards.json`**。

INI **只在 DLL 初始化时读一次**，游戏运行中改 INI 不生效。

---

## 3. 实体卡怎么给游戏

1. 接好 SKPass 原生 USB，当前固件为 1.0.0（HID 从 1.0.0 支持）；游戏使用本发行中匹配位数的 DLL。已安装 v1.0 DLL 可继续使用。
2. **在上位机点「断开」**，释放独占的数据 HID 给游戏；旧固件释放的是 CDC 串口。
3. 启动游戏。DLL 自动按 VID/PID `1CCF:5252`、Usage Page `FFCB`、Usage `1` 和 64 字节报告匹配数据 HID，再进行带会话标识的握手；不需要配置 COM 号。
4. 刷卡，游戏轮询时 DLL 把卡呈上去。

DLL 的行为边界：

* 数据 HID 每 10 ms 检查输入，每秒发送会话保活；5 秒无有效心跳会断开并重新发现。不存在匹配数据 HID 时才尝试 CDC **115200 8N1 / DTR**。
* HID 被占用或无法访问时不会回退 CDC；每秒再次探测。多台设备默认选可打开的数据 HID，不提供 INI 序列号选择。仍只服务 `unit_no=0`。
* **不读 CardIO HID**。DLL 会写入 HID 会话握手、保活和关闭命令，**不发送写卡、更新或任意 NFC 命令**；兼容 CDC 路径只接收数据。
* Windows 系统 HID/SetupAPI 已足够，不需要随 DLL 放入 Qt 或 HIDAPI DLL。完整协议见 [USB HID 说明](../docs/usb-hid.md)。

---

## 4. 给游戏的是什么数据

| 读到的结果 | DLL 的动作 |
|---|---|
| Classic 完整读数，且 48 字节载荷**偏移 38** 起的 10 字节是合法 BCD | 走 `aime_io_nfc_get_aime_id`，输出这 10 字节号码 |
| FeliCa 的 S_PAD0 解密成功 | **同一个 AiMe ID 接口**输出解出的号码（不是另开 FeliCa 号码接口） |
| FeliCa 没解出有效号码 / `status!=0` / 没有 S_PAD0 | 回退 `aime_io_nfc_get_felica_id`，输出**原始 8 字节 IDm**；DLL 不做任何 UID→号码推算 |
| Classic 读失败 | 什么都不呈，**不伪造号码** |
| 按住了刷卡键、且当前没有有效实体卡 | 呈 `cards.json` 里启用的那张虚拟卡号码 |

其它规则：

* 实体卡数据 **5 秒没更新就失效**（硬编码）；收到移卡消息 `0x11` 或 HID/串口读失败也会清空。
* **实体卡优先于虚拟卡**；虚拟卡借用 AiMe ID 通道。
* DLL 只认固定偏移 38，**不像上位机那样做候选扫描**。所以极少数卡会"上位机显示得出、游戏拿不到"，这是有意的保守行为。

---

## 5. 虚拟卡（按键呈卡）

* 在**上位机**保存并启用一张卡，并把 JSON 放到 DLL 能读到的位置。
* 按住设置的键 = 呈卡，松开 = 移卡。
* 刷卡键存的是 Windows 虚拟键码 `keyVk`。**上位机默认 Enter(13)，但 DLL 侧要求 JSON 里必须有合法 `keyVk`** —— 缺这个字段或值不在白名单里，整个 JSON 会被判为无效，虚拟卡直接不生效。
* 允许的键码范围：`8/9/13/32`、`0x21–0x28`、`0x2D/0x2E`、`0x30–0x39`、`0x41–0x5A`、`0x60–0x6F`（**排除 `0x6C`**）、`0x70–0x87`、`0xBA–0xC0`、`0xDB–0xDF`、`0xE2`。修饰键（Ctrl/Alt/Shift/Win）和鼠标键都不支持。
* 按键只在**游戏目录内的程序处于前台**时生效：以 DLL 所在目录为游戏目录，允许该目录及子目录中的前台 EXE，不限制游戏或加载器的名称。路径比较不区分大小写，并检查目录边界；切到其它目录的程序时不会触发虚拟刷卡。同目录内的工具也符合此规则。实体读卡不受前台窗口限制。
* JSON 每 **250 ms** 检查一次，改完保存通常不用重启游戏；文件上限 64 KiB。
* 按住期间即使 JSON 被改写，也继续用按下瞬间锁存的那张卡，直到松开。

---

## 6. `cards.json` 格式

建议由上位机生成。下面只是字段示例，号码是合成的测试值：

```json
{
  "version": 1,
  "activeCode": "01036261387195961452",
  "keyVk": 13,
  "cards": [
    { "type": "classic", "accessCode": "01036261387195961452", "note": "测试卡" }
  ]
}
```

DLL 的解析要求（不满足就当文件无效）：

* `version` 必须是 `1`；
* `keyVk` 必须是最多 3 位的十进制整数（`0..999`）且在白名单内；
* `activeCode` 必须是恰好 20 个 ASCII 数字、不能全 0；
* **必须存在一条 `type == "classic"` 且 `accessCode` 与 `activeCode` 完全相同**的记录；
* 只支持**一张**启用卡；`note` 字段 DLL 完全不解析（只给上位机显示）；
* 顶层字段不允许重复，文件末尾不允许有多余内容（内置解析器上限为嵌套深度 **≤ 32 层**、Token 总数 **≤ 4096 个**）。

---

## 7. 导出与兼容边界

实现的导出（共 7 个，`dll_check.c` 会逐个校验）：

| 导出 | 行为 |
|---|---|
| `aime_io_get_api_version` | 恒返回 `0x0100` |
| `aime_io_init` | 初始化读卡线程及 JSON；设备可稍后插入；重复调用直接成功；初始化失败返回 `E_FAIL` |
| `aime_io_nfc_poll` | 刷新卡状态与虚拟卡；只支持 `unit_no = 0`，否则 `E_INVALIDARG` |
| `aime_io_nfc_get_aime_id` | 需要 `luid` 和 `n == 10`；未初始化返回 `S_FALSE` |
| `aime_io_nfc_get_felica_id` | 8 字节 IDm 按大端组成 `uint64_t` |
| `aime_io_led_set_color` | **空实现**，什么都不做 |
| `aime_io_nfc_get_mifare_block` | `size != 32`、单位号非 0 或指针为空 → `E_INVALIDARG`；其余**恒返回 `S_FALSE`**，不提供原始块 |

其它边界：

* **没有** `aime_io_shutdown`（AimeIO 1.0 没有这个回调），DLL 用 `GET_MODULE_HANDLE_EX_FLAG_PIN` 把自己钉在进程里。
* DLL **不联网**：没有 AiMeDB / `aqua.naominet.live` / 任何 HTTP、socket 调用。
* 只支持 `unit_no = 0`（单读卡器）。
* 上位机编辑器只改 `[aimeio] path/cardsPath` 与 `[aime] enable`，不会动 `aimeGen`、`felicaGen` 这类分支特有项；不同 segatools 分支是否加载自定义 DLL，要各自确认。

---

## 8. 刷不上卡时查什么

| 现象 | 先查 |
|---|---|
| DLL 没被加载 | 文件是否在 INI 目录、位数是否与加载进程一致、加载器/`[aime] enable` 是否生效 |
| 按键不呈卡 | `cards.json` 路径对不对、有没有 `keyVk`、那张卡是否**已启用**、前台窗口是否被允许 |
| 实体卡不呈卡 | 上位机是否仍占用数据 HID/旧 CDC（要先断开）、固件和游戏 DLL 是否一起更新、读卡是否成功、是否已超过 5 秒 |
| 游戏显示的号码和上位机不同 | 是不是回退成了 FeliCa IDm；看数据来源，别把派生号当印号 |
| 明明有卡但游戏没反应 | 确认 VID/PID 是 `1CCF:5252`（更新模式下是 `0x5253`，那是磁盘不是读卡器） |

---

## 9. 从源码构建

需要 **Windows C++17 工具链 + CMake + Ninja，不需要 Qt 开发库**（SPAD 表以头文件形式共享 `../desktop/src/SpadTables.h`，构建时保留它和来源声明）。

在 **SKPass 根目录**执行：

```powershell
# x64（MinGW）
./scripts/build-aimeio.ps1 -Architecture x64 `
  -CompilerBin C:/Qt/Tools/mingw1310_64/bin `
  -CMake C:/Qt/Tools/CMake_64/bin/cmake.exe -Ninja C:/Qt/Tools/Ninja/ninja.exe

# x86（LLVM-MinGW）
./scripts/build-aimeio.ps1 -Architecture x86 `
  -CompilerBin D:/tools/llvm-mingw/bin -CompilerPrefix i686-w64-mingw32- `
  -CMake C:/Qt/Tools/CMake_64/bin/cmake.exe -Ninja C:/Qt/Tools/Ninja/ninja.exe
```

脚本会：配置到 **`aimeio/build-<arch>/`** → 跑 CTest（帧/协议及实际 DLL 加载）→ 校验 PE 位数（x86=`0x014c`，x64=`0x8664`）→ 复制到 `releases/v1.0/aimeio/<arch>/`。需保留 `../shared/`、`../esp32/main/hid_wire.c/.h` 与原协议源码。

导出与模拟测试不能代替真实游戏验证，当前范围见 [验证记录](../docs/verification.md)；算法与表的来源见 [第三方声明](../THIRD_PARTY_NOTICES.md)。
