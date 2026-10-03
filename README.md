# EU4 Unicode Patch

让《欧陆风云 IV》使用标准 **UTF-8** 显示和处理中文，支持扩展汉字、中文输入法、整字编辑，以及 Unicode 国家名搜索。

适用于 **EU4 1.37.5.0 Inca / Windows x64**。当前版本：**v0.1.0-experimental**。

[下载实验版](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases/download/v0.1.0-experimental/EU4UnicodePatch-1.37.5-v0.1.0-experimental-isolated.zip) · [发布说明](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases/tag/v0.1.0-experimental) · [问题反馈](https://github.com/YozoraTempest/EU4-Unicode-Patch/issues)

本版在独立游戏副本中运行，包含用于验证文字显示的测试模组。DLL 保留隔离目录保护，直接复制到正式游戏目录会拒绝启用。完整 Unicode 排版仍在开发中。

## 本版功能

- 标准 UTF-8 本地化、界面和地图文字，保留游戏颜色标记、资源图标及中文换行。
- 思源黑体 SC 与遍黑体，按需生成图集之外的新汉字，解决输入其他汉字时显示省略号的问题。
- 单行编辑的中文输入法候选窗、完整 UTF-8 提交、整字退格、键盘选区与替换；输入功能通过启动开关启用。
- 外交国家名单的 Unicode 搜索，以及中文和扩展汉字存档路径修正。

字体文件的字符覆盖已审计，普通汉字和扩展汉字已核对真实游戏纹理；各项验证的范围见[测试记录](docs/validation.md)。

## 运行

需要 **Windows 10 1903 或更新版本**、PowerShell 7，以及已安装的 EU4。游戏目录中需已有 [Matanki EU4dll](https://github.com/matanki-saito/EU4dll) 的 x64 `VERSION.dll` 加载器；发布包不附带游戏或加载器。

1. 下载实验版 ZIP，解压得到 `EU4UnicodePatch` 文件夹，保留这个文件夹名称。
2. 在解压目录打开 PowerShell 7，运行以下命令；将游戏路径改成自己的安装目录。
3. 从打开的测试游戏开始新战局，或在外交搜索框测试中文输入。

```powershell
$gameDirectory = 'D:\SteamLibrary\steamapps\common\Europa Universalis IV'
.\tools\prepare-runtime.ps1 -GameDirectory $gameDirectory
.\tools\prepare-test.ps1 -GameDirectory $gameDirectory -OpenFonts
.\tools\start-test.ps1 -ExperimentalInput
```

首次准备会复制约 5 GB 游戏文件，并在本目录创建独立的用户目录与测试模组。开源字体随包提供，无须安装系统字体。发布包已经包含 DLL 和字体生成器，运行不需要编译或安装 Python。

以后启动只需执行 `start-test.ps1 -ExperimentalInput`；省略该开关则关闭实验输入。更新、目录说明与移除方式见[运行说明](docs/install.md)。

## 兼容性

- **游戏版本：** 仅支持已适配的 1.37.5.0 x64 可执行文件，启动时检查 SHA-256 和目标指令。
- **旧双字节补丁：** 与 `plugins/plugin64.dll` 修改位置重叠，不能同时加载；准备脚本不会把正式游戏的插件复制进测试实例。
- **汉化模组：** 使用旧双字节转义的本地化需要转换成普通 UTF-8。发布包中的小型测试模组不提供整套汉化；转换方法见[开发说明](docs/development.md#旧汉化迁移)。
- **当前限制：** 每字号固定一张图集；多页、缓存淘汰及复杂文字整段排版尚未完成。阿拉伯文等需要连字或双向布局的文字不能按当前汉字路径验收。
- **尚未验证：** 所有输入法和控件、系统剪贴板完整事件链、多行编辑、长期战役、铁人及多人联机。测试模组会改变游戏校验和。

## 排查问题

加载日志位于 `private/runtime/plugins/eu4_unicode_probe.log`。正常启用时包含：

```text
UTF-8 import, UI, format, map and bitmap iterators enabled.
```

启用输入功能后，还会出现 `Experimental UTF-8 input and single-line grapheme editing enabled.`。日志出现 `Refused` 表示路径、版本或旧插件检查未通过；没有日志时检查 DLL 位置和加载器。

出现省略号时，确认使用 `-OpenFonts` 准备字体，并查看日志是否报告字库缺字或图集容量不足。准备文件前先退出测试游戏。

遇到崩溃、乱码或输入异常，请[提交 Issue](https://github.com/YozoraTempest/EU4-Unicode-Patch/issues/new)，附上游戏版本、复现步骤、模组列表和补丁日志。

## 开发

源码使用 C++17、MSVC、MASM 和 Windows SDK。[构建与检查](docs/build.md) · [字体说明](docs/open-fonts.md) · [引擎适配与复验](docs/development.md) · [后续任务](docs/roadmap.md)

## 许可证

项目代码采用 [MIT](LICENSE)。思源黑体和遍黑体采用 SIL OFL 1.1，字体版本、授权与其他依赖见[第三方说明](THIRD_PARTY_NOTICES.md)。
