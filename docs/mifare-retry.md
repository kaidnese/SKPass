# MIFARE Classic 读取间隔与软重试

本次修改只涉及 ESP32 的 Classic 读取流程。现有读取范围仍为扇区 0 的 Block 0、1、2；没有新增全卡扫描、写操作或读取扇区尾块。

## 行为

- 每次 READ 前、重新选卡前及认证前加入最小 5 ms 的任务延时。按系统节拍向上取整，并补一个节拍，避免当前节拍已部分过去导致等待不足。
- 当前 `CONFIG_FREERTOS_HZ=100`，实际请求等待 2 ticks，通常约 10～20 ms。直接使用 `pdMS_TO_TICKS(5)` 在这个配置下会得到 0，不能实现所需间隔。
- 免认证试读保留原行为，正常的拒绝响应直接转入 Key A 认证，不重复发送免认证 READ。
- 每种 Key A 的初始认证最多尝试 3 次（初次 + 2 次补试）。配置 Key A 认证未成功时保留工厂 `FFFFFFFFFFFF` 回退；两者相同则不重复尝试同一密钥。
- 认证成功后，每块最多有 3 次尝试机会。补试包含重新选卡、重新认证和重新 READ；补试认证失败也消耗一次机会，避免嵌套重试放大等待时间。
- 重试用当前成功的 Key A，只重读失败块。已经取得的块不重复读取，完整成功后才上报 48 字节。
- 短响应、错误 RF 状态及超时都不会作为有效块数据上报。最终失败清空部分扇区数据，保留 UID 和原有错误状态。
- 重新选卡失败（包括卡片已离开、UID 变化或 NCI 同步失败）立即停止补试，由原有主循环执行 PN7150 硬件恢复；其它失败耗尽补试后才进入原有恢复流程。

## 为什么补试前重新选卡

这属于不复位 PN7150 的软件恢复，但并非强行沿用已经失败的 Crypto1 会话。READ 或认证失败后，卡片可能已进入不能继续读取的状态。

驱动使用 `RF_DEACTIVATE(Sleep)` → 等待通知 → `RF_DISCOVER_SELECT` → 等待激活通知，并核对 UID、协议和接口。控制命令等待会跳过迟到的旧 DATA；新的激活通知重新提供 RF connection credits。之后重新认证，再读取失败块。

NCI DATA 没有每次 READ 的事务编号。在接收超时后直接重发 READ，迟到数据可能被错当成下一块数据，因此没有采用这种盲重发方式。若驱动不能完成 Sleep/Select 同步，就停止补试。

参考来源：

- [ElectronicCats MifareClassic_read_block 示例](https://github.com/ElectronicCats/ElectronicCats-PN7150/blob/master/examples/MifareClassic_read_block/MifareClassic_read_block.ino)：嵌入 Key A 的 `40 sector 10 key` 认证、`10 30 block` 读取以及末字节状态检查。
- [ElectronicCats readerReActivate 实现](https://github.com/ElectronicCats/ElectronicCats-PN7150/blob/master/src/Electroniccats_PN7150.cpp)：Sleep 后重新 Select。本工程额外等待对应响应/通知，并使用实际 discovery ID 核对同卡。

5 ms 间隔和 2 次补试是本工程的容错策略，并非上述示例要求的时序参数。它们有机会改善偶发通信失败，不能修复错误密钥、访问权限限制或严重的射频耦合问题。

## 实机观察

重新编译并烧录本工程，再保持卡片位置不变，对比修改前后的完整成功次数和 `NFC_RECOVERY` 次数。上位机与 USB 协议无需更新。

`MIFARE block=...`、`MIFARE AUTH ...` 保留原始数据/状态，新增 `attempt` 和命令开始时刻 `start_ms`。使用命令开始时刻比较连续读取间隔，日志行的时间是完成收发之后的时间。

偶发失败挽回时应依次看到：

```text
MIFARE block=01 result=ESP_ERR_TIMEOUT ... attempt=1/3 start_ms=...
MIFARE RETRY block=01 attempt=2/3 action=reselect_reauth
MIFARE AUTH key_slot=... result=ESP_OK size=2 status=00 attempt=2/3 start_ms=...
MIFARE block=01 result=ESP_OK raw=... attempt=2/3 start_ms=...
MIFARE RETRY recovered; continuing sector read
MIFARE block=02 result=ESP_OK raw=... attempt=1/3 start_ms=...
```

最终失败则会出现 `MIFARE RETRY exhausted or RF session lost` 或 `MIFARE RESELECT ... soft retry aborted`，随后由主循环输出 `NFC_RECOVERY`。补试可能增加失败卡的等待时间，但尝试次数和驱动命令超时均有限。

## 已验证与未验证

已用本机 GCC、`-Wall -Wextra -Werror` 编译并运行真实 `aime_reader.c` / `mifare_reader.c` 配合模拟 PN7150；100 Hz 与 1000 Hz 节拍下各通过 14 个场景。覆盖认证超时、双密钥回退、单块 NACK/超时/短帧/错误 RF 状态、补试耗尽、重选失败、补试认证失败、成功数据正确及失败清空。原写卡模拟测试、CRC/帧与 HID 测试也通过。

模拟重新选卡测试验证调用方在重选失败时停止；不等于硬件已验证迟到 DATA 排空或真实 Crypto1 恢复。ESP32 固件已于 2026-10-11 使用 ESP-IDF 5.2.5 在 `build/` 编译通过，并通过分区大小检查及镜像校验；未烧录、未验证实际天线条件下的改善幅度。

从工程根目录复跑读卡模拟测试：

```powershell
cd esp32  # 从SKPass根目录进入固件目录后执行
New-Item -ItemType Directory -Force build-mifare-tests | Out-Null
gcc -std=c11 -Wall -Wextra -Werror -I tests/mifare_stubs -I main tests/mifare_reader_tests.c main/mifare_reader.c main/aime_reader.c -o build-mifare-tests/mifare_reader_tests.exe
.\build-mifare-tests\mifare_reader_tests.exe
```

加 `-DportTICK_PERIOD_MS=1` 可复跑 1000 Hz 模拟。`tests/mifare_stubs` 只用于本机测试，不加入 ESP-IDF 的 include 路径。
