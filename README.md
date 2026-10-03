# EU4 Unicode Patch

让《欧陆风云 IV》使用标准 **UTF-8** 显示文字，支持中文输入法、整字退格与选区替换。

作者：**VulonLok**。

适用于 **EU4 1.37.5.0 Inca / Windows x64**。

[正式版与可选字体包](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases/latest) · [Nightly 测试版](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases) · [更新记录](CHANGELOG.md) · [问题反馈](https://github.com/YozoraTempest/EU4-Unicode-Patch/issues)

## 功能

- **标准 UTF-8**：本地化和编辑文本使用标准 UTF-8，便于直接编辑、维护和分享汉化文件。
- **生僻字支持**：支持“𠮷”等补充平面汉字，字形由所用字体提供。
- **中文输入与编辑**：支持中文输入法候选窗，光标移动、退格和选区替换按完整字符处理。
- **系统字体优先**：字形按需生成，额外字库作为可选包提供；保留模组修改字体、字号和字宽的能力。

## 与双字节补丁的区别

以下对比以 [EU4dll Release 93](https://github.com/matanki-saito/EU4dll/releases/tag/93) 的 Windows x64 实现为参考。

| 对比项 | 双字节补丁 | EU4 Unicode Patch |
| --- | --- | --- |
| 文本表示 | UTF-8 输入转换为自定义双字节转义 | 本地化和编辑文本保留标准 UTF-8 |
| 字符处理 | 以 16 位字符和字形索引处理文字 | 按 Unicode 码点处理，支持补充平面汉字 |
| 输入与编辑 | 支持文字输入与退格 | 支持输入法候选窗，按字素边界处理光标、退格与选区替换 |
| 字体 | 使用预生成的 `.fnt` 和位图字库 | 系统字体优先，图集按需生成；字库可选，模组字体仍可覆盖 |

按需补字用于补丁生成的图集；模组自带位图字体仍使用原字库。

## 安装

需要 **Windows 10 1903 或更新版本**。包内已包含 `VERSION.dll` 加载器；已有该文件时，先备份原文件。

1. 保存战役并完全退出游戏。
2. 下载补丁 ZIP，将里面的全部内容复制到 `eu4.exe` 所在目录，合并文件夹并覆盖同名文件。Source code 是源码。
3. 正常从 Steam 或启动器启动游戏。

安装后的目录：

```text
Europa Universalis IV/
├── eu4.exe
├── VERSION.dll
├── plugins/
│   ├── eu4_unicode_patch.dll
│   └── eu4_unicode_patch/
│       └── LICENSE.txt
└── EU4UnicodePatch.README.txt
```

更新时，退出游戏后用新包覆盖同一目录。安装不需要运行脚本或安装系统字体。不要把整个 ZIP 放进 `plugins`，也不要多套一层文件夹。

系统字体缺字时，再下载可选字体包，同样解压覆盖到游戏目录后重启。已安装字体包时仍优先使用系统字体；移除 `plugins/eu4_unicode_patch/fonts/` 即可取消可选字库。字体缓存由补丁在启动时生成于 `gfx/fonts/eu4-unicode/cache/`，无需另行下载。

从 v0.1.1 更新时，原有 `fonts/` 文件夹会继续作为可选字库使用；只用系统字体时可删除它。

## 卸载

退出游戏，删除 `plugins/eu4_unicode_patch.dll`、`plugins/eu4_unicode_patch/`、`plugins/eu4_unicode_patch.log`、`gfx/fonts/eu4-unicode/`、`EU4UnicodePatch.README.txt` 和可选包的 `EU4UnicodePatch.FONTS.txt`。恢复备份的 `VERSION.dll`；此前没有加载器时，删除本包的 `VERSION.dll`。其他插件仍需加载器时，保留或重新安装所需加载器。

恢复旧加载器后，旧双字节补丁及其自动更新也会恢复原来的行为。

## 兼容性

- **游戏版本：** 仅支持 1.37.5.0 Inca，Windows x64。补丁会检查 EXE 的 SHA-256 和目标指令，校验失败时拒绝应用。
- **旧双字节补丁：** 包内加载器跳过 `plugins/plugin64.dll`、开发探针 `eu4_unicode_probe.dll` 和旧补丁的自动更新，旧文件保留。其他插件仍正常加载；已测试与 MenuPatch 一起启动。安装 Unicode 补丁期间不要换回旧加载器，以免同时加载两套补丁。
- **汉化模组：** 本地化应使用普通 UTF-8。旧双字节转义汉化需要先[转换](docs/development.md#旧汉化迁移)。模组的 `.fnt` 字宽、字号与位图字形保留，包括对原版同名路径的覆盖；缺字动态生成目前用于补丁自身图集。
- **当前限制：** 地图图集有内存上限，UI 图集仍为单页；阿拉伯文等复杂排版、所有控件及输入法、长期战役、铁人和联机仍待完善。详见[测试记录](docs/validation.md)。

## 排查问题

加载日志位于 `plugins/eu4_unicode_patch.log`。正常启用时包含：

```text
UTF-8 import, UI, format, map and bitmap iterators enabled.
```

没有日志时，检查文件位置和加载器。日志出现 `Refused` 表示版本或冲突检查未通过；出现缺字占位时，尝试安装可选字体包，并查看是否有图集容量错误。

遇到崩溃、乱码或输入异常，请[提交 Issue](https://github.com/YozoraTempest/EU4-Unicode-Patch/issues/new)，附上游戏版本、模组列表、复现步骤和补丁日志。

## 交流

[加入 QQ 交流群：欧陆风云 · 永夜的星月回廊](https://qm.qq.com/q/Csnqqd8rUO)

## 构建

源码使用 C++17、MSVC、MASM 和 Windows SDK。构建、保护检查和打包命令见[构建说明](docs/build.md)。

## 许可证

[MIT](LICENSE) © 2026 VulonLok。可选字体包中的思源黑体与遍黑体采用 SIL OFL 1.1，见[第三方说明](THIRD_PARTY_NOTICES.md)。
