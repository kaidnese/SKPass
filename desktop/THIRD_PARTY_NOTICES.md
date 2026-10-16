# 第三方参考与数据来源

* `src/SpadTables.h` 的2304个表项来自用户提供的 `micetools-ref/spad.c`：Bottersnike/micetools，`src/micetools/lib/mice/spad.c`，符号 `SPAD0_SPN_TABLE`。与本地来源逐项比对一致。公开来源：<https://gitea.tendokyu.moe/Bottersnike/micetools/raw/branch/master/src/micetools/lib/mice/spad.c>。
* SPAD/Classic算法依据用户提供的 `aime-spad.py`、`aime-classic-code.py` 与 micetools 的 spad/solitaire/felica 实现移植；Classic计算器参考 <https://blog.naivetomcat.cn/pages/aimecalc/>，KEY取源码的 `A1B3E86CF02974D6`。
* PN7150 NCI DATA封装与MIFARE命令依据 ElectronicCats/NXP衍生实现，作者 Salvador Mendoza 与 Electronic Cats：<https://github.com/ElectronicCats/ElectronicCats-PN7150>，特别是 `examples/MifareClassic_read_block/MifareClassic_read_block.ino`。其示例头标记 beerware、无担保；本工程以其为协议行为参考，没有整体复制该示例。
* AiMe/AICC背景与DFC映射参考用户文档和 <https://sega.bsnk.me/allnet/amusement_ic/>。
* 2.1-write的两阶段MIFARE写入依据同仓库 `examples/MifareClassic_write_block/MifareClassic_write_block.ino`，使用PN7150分支的尾状态00与固定 `10 A0 block` / `10 data16` 流程；重新激活依据其 `readerReActivate`，使用实际discovery ID。

提供的 micetools 本地片段未包含完整仓库授权文件，本次尝试获取仓库根 LICENSE 返回404。**没有将第三方表项擅自重新许可为MIT或其它协议。对外发布/分发前需要核实 micetools 对应版本的授权条件并保留所需版权与许可声明。** 本说明不替代上游许可证。
