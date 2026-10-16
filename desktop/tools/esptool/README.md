# ROM 下载工具

此目录用于准备 Espressif 官方 esptool 4.12.0 Windows x64 可执行程序。发行 EXE 内置它，用户无需安装 Python；源码不要求提交下载的 EXE。

在 SKPass 根目录执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-esptool.ps1`。脚本从 [官方发行页](https://github.com/espressif/esptool/releases/tag/v4.12.0) 下载、核对 SHA256，再保留 `esptool.exe`、上游 `LICENSE` 和 `README.md` 到 `esptool/` 子目录。

官方 ZIP SHA256：`42fddc5e6a05716868ad77fb43acbf53be041f97abed87ff850df1dc88140889`。
其中 `esptool.exe` SHA256：`1cd8386a7934862eb6ce1e14af3cc2309b0a743de1171fd09fab2f2ef2f5d1a3`。

运行时，下载临时文件与 PyInstaller 解包目录均使用上位机运行目录内的 `rom-work/update-*/`，成功或失败结束后清理本次临时目录；不使用 C 盘默认 TEMP。开发下载缓存留在 `desktop/build/tools-downloads/`。

esptool 使用 GPL-2.0-or-later，完整上游许可随程序打包。上游源码：<https://github.com/espressif/esptool/tree/v4.12.0>。其它算法与组件声明见项目根 `THIRD_PARTY_NOTICES.md`。
