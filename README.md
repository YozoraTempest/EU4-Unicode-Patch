# EU4 Unicode Patch

让《欧陆风云 IV》使用标准 **UTF-8** 显示文字，支持中文输入法、整字退格与选区替换。

适用于 **EU4 1.37.5.0 Inca / Windows x64**。当前版本：**v0.1.2-experimental**。

[下载补丁](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases/download/v0.1.2-experimental/EU4UnicodePatch-1.37.5-v0.1.2-experimental-drop-in.zip) · [可选字体包](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases/download/v0.1.2-experimental/EU4UnicodePatch-fonts-v0.1.2-experimental.zip) · [发布说明](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases/tag/v0.1.2-experimental) · [问题反馈](https://github.com/YozoraTempest/EU4-Unicode-Patch/issues)

## v0.1.2 更新

- 优先使用玩家系统字体，主包不再附带完整字库。
- 思源黑体 SC 和遍黑体改为可选字体包，用于补充系统缺少的字形。
- 简化第三方说明。

本版仍为实验版。支持 UTF-8 文字，不附带完整汉化；复杂文字排版、铁人和多人联机尚未完成验证。

## 安装

需要 **Windows 10 1903 或更新版本**。包内已包含 `VERSION.dll` 加载器；已有该文件时，先备份原文件。

1. 保存战役并完全退出游戏。
2. 下载补丁 ZIP，将里面的全部内容复制到 `eu4.exe` 所在目录，合并文件夹并覆盖同名文件。
3. 正常从 Steam 或启动器启动游戏。

安装后的目录：

```text
Europa Universalis IV/
├── eu4.exe
├── VERSION.dll
├── plugins/
│   ├── eu4_unicode_patch.dll
│   └── eu4_unicode_patch/
│       └── licenses/
└── gfx/fonts/eu4-unicode/
```

更新时，退出游戏后用新包覆盖同一目录。安装不需要运行脚本或安装系统字体。详细目录与移除方法见[安装说明](docs/install.md)。

系统字体缺字时，再下载可选字体包，同样解压覆盖到游戏目录后重启。已安装字体包时仍优先使用系统字体；移除 `plugins/eu4_unicode_patch/fonts/` 即可取消可选字库。主包保留游戏加载所需的基础图集。

## 卸载

退出游戏，删除 `plugins/eu4_unicode_patch.dll`、`plugins/eu4_unicode_patch/`、`gfx/fonts/eu4-unicode/`、`EU4UnicodePatch.README.txt` 和可选包的 `EU4UnicodePatch.FONTS.txt`。恢复备份的 `VERSION.dll`；此前没有加载器时，删除本包的 `VERSION.dll`。

## 兼容性

- **游戏版本：** 仅支持 1.37.5.0 Inca，Windows x64。补丁会检查 EXE 的 SHA-256 和目标指令，校验失败时拒绝应用。
- **旧双字节补丁：** 包内加载器跳过 `plugins/plugin64.dll` 和旧补丁的自动更新，旧文件保留。其他插件仍正常加载；已测试与 MenuPatch 一起启动。
- **汉化模组：** 本地化应使用普通 UTF-8。旧双字节转义汉化需要先转换；自定义模组字体不保证支持。
- **本次测试：** 无字库主包启动，系统字体与可选字库两种模式的五种字号 D3D9 上传。完整输入与候选窗有此前开发版记录，本版尚未完成整套人工复验。
- **当前限制：** 每字号一张固定图集；多页图集、阿拉伯文等复杂排版、所有控件及输入法、长期战役、铁人和联机仍待完善。详见[测试记录](docs/validation.md)。

## 排查问题

加载日志位于 `plugins/eu4_unicode_patch.log`。正常启用时包含：

```text
UTF-8 import, UI, format, map and bitmap iterators enabled.
```

没有日志时，检查文件位置和加载器。日志出现 `Refused` 表示版本、冲突或包内文件检查未通过；出现缺字占位时，尝试安装可选字体包，并查看是否有图集容量错误。

遇到崩溃、乱码或输入异常，请[提交 Issue](https://github.com/YozoraTempest/EU4-Unicode-Patch/issues/new)，附上游戏版本、模组列表、复现步骤和补丁日志。

## 交流

[加入 QQ 交流群：欧陆风云 · 永夜的星月回廊](https://qm.qq.com/q/Csnqqd8rUO)

## 构建

源码使用 C++17、MSVC、MASM 和 Windows SDK。构建、保护检查和打包命令见[构建说明](docs/build.md)。

## 许可证

[MIT](LICENSE) © 2026 EU4UnicodePatch contributors。思源黑体与遍黑体采用 SIL OFL 1.1，见[第三方说明](THIRD_PARTY_NOTICES.md)。
