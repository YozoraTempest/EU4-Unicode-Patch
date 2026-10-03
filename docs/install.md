# 安装与卸载

下载 Release 中的 `EU4UnicodePatch-1.37.5-v0.1.3-experimental-drop-in.zip`。GitHub 自动生成的 Source code 是源码，不是玩家补丁。

## 安装

1. 保存战役并退出游戏。游戏目录已有 `VERSION.dll` 时，先备份它。
2. 把 ZIP 内全部内容复制到 `eu4.exe` 所在目录，合并文件夹并覆盖同名文件。
3. 正常启动游戏。

应能找到：

```text
VERSION.dll
plugins/eu4_unicode_patch.dll
gfx/fonts/eu4-unicode/zh-hans-14.fnt / .dds
gfx/fonts/eu4-unicode/zh-hans-16.fnt / .dds
gfx/fonts/eu4-unicode/zh-hans-18.fnt / .dds
gfx/fonts/eu4-unicode/zh-hans-24.fnt / .dds
gfx/fonts/eu4-unicode/zh-hans-map.fnt / .dds
```

无需脚本、编译或系统字体安装。不要把 ZIP 整个放进 `plugins`，也不要多套一层 `EU4UnicodePatch` 文件夹。

## 可选字体包

补丁优先使用系统字体。需要生僻字或系统缺少的汉字时，下载 `EU4UnicodePatch-fonts-v0.1.3-experimental.zip`，退出游戏后将其中全部内容复制到同一游戏目录，再重新启动。

三个字体文件放在 `plugins/eu4_unicode_patch/fonts/`，仅供游戏使用。安装后仍优先使用系统字体；删除这个文件夹即可移除可选字库。

## 旧补丁与汉化

包内加载器保留其他插件的加载方式，跳过旧 `plugin64.dll`、开发探针 `eu4_unicode_probe.dll` 和旧双字节补丁的自动更新脚本。旧文件不删除。换回旧加载器可能重新加载双字节插件，与 Unicode 补丁冲突。

本包不包含中文翻译。本地化必须使用普通 UTF-8；旧转义汉化的转换方法见[开发说明](development.md#旧汉化迁移)。模组使用自定义字体路径时需要单独适配。

## 更新

退出游戏后，把新版玩家包覆盖到同一目录。

从 v0.1.1 更新时，原有 `fonts/` 文件夹会作为可选字库继续使用；只用系统字体时可删除它。

## 卸载

退出游戏，删除以下补丁文件：

```text
plugins/eu4_unicode_patch.dll
plugins/eu4_unicode_patch/
plugins/eu4_unicode_patch.log
gfx/fonts/eu4-unicode/
EU4UnicodePatch.README.txt
EU4UnicodePatch.FONTS.txt
```

恢复备份的 `VERSION.dll`；此前没有加载器时，删除本包的 `VERSION.dll`。其他插件需要加载器时保留或重新安装所需加载器。恢复旧加载器后，旧双字节补丁及其自动更新恢复原来的行为。

日志与问题反馈见 [README](../README.md#排查问题)。
