# SKPass 命名与兼容迁移

固件工程名 `SKPass`、版本 1.0.0；USB 字符串、日志、自有组件与 Qt 标题/侧栏使用 SKPass。当前 EXE 为 `SKPass-v1.0.exe`，DLL 为 `aimeio_skpass.dll`。ROM 模式使用芯片原厂 USB 身份，不修改它；`skpass_cdc` 内部 API 保留，处理共享 HID/CDC。

保留以下兼容常量，而非遗漏改名：

| 项目 | 保留内容 | 原因 |
| --- | --- | --- |
| HID | 9字节、Report ID 1/2、usage page 0xFFCA | 原CardIO客户端仍可使用 |
| USB | 1CCF:5252，CardIO接口0、CDC接口1/2 | 数据 HID 新增接口3，匹配独立 Usage Page 0xFFCB |
| 序列号 | CARDIOP1 / CARDIOP2 | 部分既有HID程序可能按该值选择玩家 |
| MSC | 1CCF:5253、UF2 family 0xC47E5767 | 不改变USB端点和UF2二进制格式 |
| CDC | CA5A、协议1、原消息ID和字段布局 | 实体卡、写卡、旧客户端协议兼容 |
| JSON | cards.json schema1、activeCode/keyVk/cards | 虚拟卡与既有游戏配置可继续使用 |
| AimeIO | aime_io_*导出、API1.0 | segatools ABI要求，不能改为SKPass导出名 |

SKPass 上位机接受 `SKPass-` 和旧 `pn7150_cardio` 心跳、旧 UF2 Board-ID 与旧镜像，保留设备协议兼容性。v1.0 首次正常启动继承此前开发版的 JSON/游戏目录偏好；不复制卡号到工程目录。

新磁盘名为 `SKPASS_UF2`，Board-ID为 `SKPASS-S3-N16R8-UF2-V1`。旧设备2.4.0及之前的更新器拒绝新SKPass项目名，所以**第一次迁移必须完整烧录**。新SKPass更新器可接受SKPass镜像和旧镜像用于回退；回退后再次升级仍受旧更新器限制。

发行版本统一为 v1.0，旧发行目录与旧打包资源已删除。卡片解析算法、RF 参数及 AimeIO API 不变。个人卡片、游戏 DLL 和 INI 不会自动删除或替换。
