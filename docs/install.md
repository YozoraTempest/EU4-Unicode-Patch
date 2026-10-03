# 运行与更新

v0.1.0-experimental 使用独立游戏副本。发布包包含已构建的 DLL、字体生成器、三个开源字体、测试夹具和准备脚本。

## 首次运行

需要 Windows 10 1903+、PowerShell 7、EU4 1.37.5.0 x64，以及原游戏目录中已有的 x64 `VERSION.dll` 插件加载器。加载器来源为 [Matanki EU4dll](https://github.com/matanki-saito/EU4dll)，不随本项目分发。

解压后进入 `EU4UnicodePatch` 文件夹，保留该名称；在 PowerShell 7 运行：

```powershell
$gameDirectory = 'D:\SteamLibrary\steamapps\common\Europa Universalis IV'
.\tools\prepare-runtime.ps1 -GameDirectory $gameDirectory
.\tools\prepare-test.ps1 -GameDirectory $gameDirectory -OpenFonts
.\tools\start-test.ps1 -ExperimentalInput
```

将第一行替换为自己的游戏安装路径。准备副本需要约 5 GB 额外空间。脚本从原游戏读取资源，生成测试模组与独立用户目录；原游戏的 `plugins` 文件夹不会复制。`-OpenFonts` 使用包内字体，缺少包内文件时才从固定上游版本下载。字体不安装到系统。

准备完成后的主要目录：

```text
EU4UnicodePatch/
├── build/                  # 已构建 DLL 和字体生成器
├── fonts/                  # 思源黑体 SC、遍黑体 P1/P2
├── tools/                  # 准备与启动脚本
└── private/
    ├── runtime/            # 独立游戏副本，插件位于 plugins/
    ├── test-mod/           # 生成的测试模组和字体图集
    └── test-userdir/       # 此实例的设置、日志和存档
```

测试模组只修改少量菜单/国家名，并在新战局开始时触发文字测试事件，不是整套汉化。`-ExperimentalInput` 开启单行 UTF-8 输入与候选窗修补；普通 `start-test.ps1` 不启用这项实验。

## 更新

退出测试游戏，将新发布包解压到原 `EU4UnicodePatch` 目录并覆盖包内文件。保留 `private/test-userdir` 即可保留测试存档和设置。再次运行 `prepare-test.ps1 -GameDirectory $gameDirectory -OpenFonts`，然后启动；通常不需要重新复制游戏资源。

`-SystemFonts` 可重新生成仅覆盖夹具字符的系统字体图集；不传字体开关的工坊模式需要已有工坊字体。一般交互测试使用 `-OpenFonts`。

## 停用与移除

关闭测试游戏即可停用此实例的补丁。需要移除时，先保存所需的 `private/test-userdir/save games`，再删除独立的 `EU4UnicodePatch` 目录。

## 常见问题

| 现象 | 检查方式 |
| --- | --- |
| 启动脚本报告版本不匹配 | 核对游戏是 1.37.5.0 x64；确切 EXE 指纹见[构建说明](build.md#版本保护) |
| 日志报告隔离目录错误 | 路径应为 `EU4UnicodePatch/private/runtime/eu4.exe` |
| 没有补丁日志 | 检查副本的 `VERSION.dll` 和 `plugins/eu4_unicode_probe.dll` |
| 输入汉字出现省略号 | 退出实例后用 `-OpenFonts` 重建；检查字体文件校验、缺字和图集容量日志 |
| 看不到输入法候选窗 | 确认启动时传入 `-ExperimentalInput`；其他输入法/控件尚需复验 |

日志路径为 `private/runtime/plugins/eu4_unicode_probe.log`。反馈时提供复现步骤、游戏版本、模组和日志；[Issues](https://github.com/YozoraTempest/EU4-Unicode-Patch/issues)。
