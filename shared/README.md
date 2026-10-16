# SKPass 共享 Windows HID 层

`windows_hid.c/.h` 给 Qt 上位机与 x86/x64 AimeIO 提供相同的设备发现和传输实现。只依赖 Windows 系统 HID、SetupAPI 与标准 C，不需要 Qt 或第三方 HIDAPI 运行库。

协议分片代码在 `../esp32/main/hid_wire.c/.h`，由固件与 Windows 一起编译，避免两个版本的格式漂移。业务帧 CRC 与卡号解析仍由各自现有模块处理。完整协议见 [USB HID 说明](../docs/usb-hid.md)。

| API | 作用 |
| --- | --- |
| `skpass_hid_list` | 枚举 `1CCF:5252` / `FFCB:1`、输入输出均 64 字节的 HID，提供设备路径和序列号 |
| `skpass_hid_open` | 独占打开，可按序列号选择；返回 1 成功、0 未发现、-1 占用/不可访问 |
| `skpass_hid_read` | 异步读取报告，1 收到、0 等待、-1 断线/错误；调用者定时轮询 |
| `skpass_hid_send` | 分片并通过 overlapped `WriteFile` 发送业务帧；调用者指定超时，当前客户端使用 250 ms |
| `skpass_hid_close` | 取消并完成挂起读操作，关闭句柄；会话 CLOSE 消息由调用者先发送 |

枚举使用只查询元数据的共享句柄，不读取报告。通讯句柄使用 `share=0` 独占；打开后扩大输入缓存、清空旧报告，生成新的会话标识。异步取消后等待操作完成再释放缓冲区，避免缓冲区失效。

一个连接只由一个线程使用；这层不提供多线程锁、多个 PC 客户端广播、业务重试或 UI。HELLO/保活/心跳确认由 `desktop/src/SerialReader.cpp` 与 `aimeio/aimeio.c` 管理。

构建时一起编译 `windows_hid.c`、`hid_wire.c`，包含本目录与 `esp32/main/`，链接 `hid` 和 `setupapi`。两个工程的 CMake 已配置；从仓库根目录构建，不能只单独复制一个源码文件夹。
