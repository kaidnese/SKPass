# 智能 NFC 信息

SKPass 在原有 CardIO、AimeIO 和街机卡读写流程之外，提供一个只读的 NFC 信息探测器。探测在一次卡片激活后的 NFC 任务中进行，所有交换都有时限和数量上限；失败会保留已取得的字段，不会因为一条公开命令失败而把 PN7150 当作永久故障。

## 支持范围

- `T4T / ISO-DEP (0x04)`：尝试全国交通一卡通/交通类 AID、公开的深圳通文件、PBOC/EMV PPSE 和公开记录。
- `T3T / FeliCa (0x03)`：保留 Amusement IC `88B4` / `FE00` 的原有读取，同时探测交通系统和公开历史块。
- `T2T (0x02)`：读取 Capability Container 和公开的 `GET_VERSION`，报告 NTAG213/215/216 或 Ultralight 的容量。
- `T5T / ISO15693 (0x06)`：读取 `Get System Information` 的块数和块大小。
- `MIFARE Classic (0x80)`：保留原有 Aime 扇区读取，并报告 SAK 对应的 1K、4K 或 Mini 几何信息。

Qt 页面将所有 `transit_*` 内部类型统一显示为 **交通卡**。具体协议、内部探测类型和原始记录仍保存在 `0x12` JSON 中，便于调试。银行卡显示为“银行卡（EMV / PBOC）”，Amusement IC 仍显示为街机卡信息。

## 数据限制

探测只发送公开、免密的 SELECT/READ、FeliCa Check/Read 和卡片信息命令，不进行支付、密钥认证、密钥破解、写入或卡模拟。交通卡、银行卡是否开放卡号、余额和交易记录由卡片发行方决定，因此字段可能为“未取得”。八达通旧格式余额使用历史公开公式并明确标记为“估算”，不能作为结算依据。

智能探测不会把交通卡的 IDm/UID 派生为街机 Access Code，也不会污染虚拟卡管理。已识别的 Amusement IC 和 MIFARE Classic 仍走原来的 CardIO/AimeIO 业务。

## `0x12` 详情帧

详情使用现有 `CA 5A` 二进制帧，消息号为 `0x12`，负载是单行 JSON，单片不超过 512 字节：

```json
{"schema":1,"scan":12,"uid":"011E65740A055390","part":0,"count":2,"kind":"transit_japan","status":"ok","protocol":3,"interface":1,"currency":"JPY","balance":1234}
```

`part=0` 是摘要；`part=1..count-1` 是交易记录。Qt 端只在收到同一 `scan`、同一 UID 且所有分片齐全后显示，并拒绝旧 scan、重复分片、UID 不一致、越界字段和超长 JSON。CDC 与数据 HID 使用相同的帧内容。

详情页的物理 UID/IDm、卡号、余额、币种、有效期、发卡机构、应用标签、存储容量和最近交易彼此独立展示；未取得的值不会用猜测替代。

| 字段 | 含义 |
| --- | --- |
| `schema` / `scan` | 结构版本 1 / 本次扫描序号，重连时重新建立序号状态 |
| `uid` / `part` / `count` | 激活的物理 UID 或 IDm 十六进制 / 分片索引 / 总分片数（1–11） |
| `kind` / `status` | 摘要中的内部子类型 / `ok`、`partial`、`unrecognized`、`rf_error` |
| `number` / `issuer` / `label` | 已解析的公开卡号、发卡机构字段、EMV 应用标签；缺失表示未取得 |
| `start` / `expiry` | 摘要中的启用日/截止日，`YYYYMMDD` |
| `balance` / `currency` | `CNY`、`HKD` 使用最小货币单位（分），`JPY` 使用日元整数；`estimate=true` 表示估算 |
| `blocks` / `block_size` / `sectors` | 存储卡的块数、每块字节数、Classic 扇区数 |
| `pages` / `first_page` / `last_page` | Type 2 总页数与可识别的用户区页范围，不保证写权限 |
| 交易分片 | `date`、`type`、可选 `amount` / `balance`、`entry` / `exit` 与原始 `raw`；未解析的类型和站点保留代码 |

扫描文档和消息号独立于旧 `0x10` 街机事件，便于以后增加卡型与详情字段。当前上位机容许未知扩展字段，但对已使用字段进行类型、长度和范围校验。上位机集成了独立的 [高级读写协议与面板](advanced-rw.md) 与 [Type 4 NDEF 卡模拟](card-emulation.md)。公开探测器仍只调用有界公开读取。

## 公开实现依据

以下来源用于核对报文字节序和文件布局，未整体复制上游读卡器：

- [nfcpy Type 3](https://github.com/nfcpy/nfcpy/blob/master/src/nfc/tag/tt3.py)：FeliCa Read Without Encryption、Polling、Request System Code 的长度、IDm、状态字段与小端服务码。
- [Metrodroid T-Union](https://github.com/metrodroid/metrodroid/blob/master/src/commonMain/kotlin/au/id/micolous/metrodroid/transit/china/TUnionTransitData.kt)：`0015` 的卡号在 offset 10、长度 10 字节，去掉首个格式位；日期在 20/24。
- [Metrodroid 中国交易记录](https://github.com/metrodroid/metrodroid/blob/master/src/commonMain/kotlin/au/id/micolous/metrodroid/transit/china/ChinaTrip.kt)：23 字节交易，金额 offset 5、类型 9、日期时间 16。
- [Metrodroid PAY.SZT](https://github.com/metrodroid/metrodroid/blob/master/src/commonMain/kotlin/au/id/micolous/metrodroid/transit/china/NewShenzhenTransitData.kt)：该应用序列号是 offset 16 的 4 字节小端整数；不把它误当作通用 BCD 卡号。PSE/1001 分支只在公开文件及日期符合布局时判为交通类，避免仅凭 PSE 成功就误判银行卡。
- [Metrodroid Suica](https://github.com/metrodroid/metrodroid/blob/master/src/commonMain/kotlin/au/id/micolous/metrodroid/transit/suica/SuicaTrip.kt)：历史服务 `090F`，余额 offset 10 的小端整数，日期为位编码，站点代码保留原始值。
- [Metrodroid Octopus](https://github.com/metrodroid/metrodroid/blob/master/src/commonMain/kotlin/au/id/micolous/metrodroid/transit/octopus/OctopusData.kt)：历史偏移在 2017 年由 350 变为 500。因此用户提供的 `(raw-350)/10` 只能作为历史公式估算，页面明确标注；不宣称是当前精确余额。

NTAG215 只能根据版本识别芯片型号，不能仅凭型号断言是 Amiibo；通用 Classic 几何信息也不能证明卡的发行方。尚未做真实交通/银行卡的射频验证，协议模拟测试不能替代实卡兼容性测试。
