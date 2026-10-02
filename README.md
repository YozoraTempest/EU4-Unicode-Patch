# EU4 Unicode UTF-8 原型

这是针对本机 EU4 1.37.5 Windows x64 的普通文本原型。标准 UTF-8 中文已在隔离游戏实例中通过本地化注册、测宽、绘制和换行，主菜单文本与普通按钮均已进行实际画面验证。

当前产物是研究用 DLL `build/eu4_unicode_probe.dll`，还不是可以覆盖原双字节补丁的完整发行版。它只接受本项目 `private/runtime/eu4.exe` 测试副本；不在正式游戏目录安装。

## 已完成

- 本地化注册入口保留原始 UTF-8 值，绕开 UTF-8 → 旧单字节转换。
- 使用 UTFCPP 解析 1–4 字节 UTF-8，以 `uint32_t` 表示 Unicode 标量。
- 主文字、普通按钮和三条位图字体路径按完整字符推进，保留原文的 UTF-8 字节。
- 在当前主文字路径中允许中文在整字边界换行。
- 为现有 BMFont 扩大分配与字符 ID 范围，零初始化字形表。
- 缺字显示省略号占位；换行等控制字符保留控制语义。
- 启用前校验 EXE SHA-256、所有修改点的原始指令、测试目录及旧补丁冲突。

运行证据见 [validation.md](docs/validation.md) 和 [游戏截图](docs/evidence/utf8-prototype.jpg)。

## 本机使用

本机已经有编译产物、独立游戏副本与测试用户目录。修改 [测试本地化源文件](fixtures/localisation/eu4_unicode_probe_l_english.yml) 中的普通文本，然后在 PowerShell 执行：

```powershell
cd D:\Astra-Paradox\repos\EU4UnicodePatch
.\tools\prepare-test.ps1
.\tools\start-test.ps1
```

准备脚本将源 YAML 写为 UTF-8 BOM，以符合游戏文件解析器的要求。内容直接写中文，不使用双字节补丁转义协议。修改后需退出并重新启动测试实例。

生成的私有字体夹具来自本机工坊模组 `2976470733`；仅在测试副本中使用。原始汉化字体中 U+0100–U+09FF 的少数字形 ID 会与游戏字体对象的成员重叠，因此准备脚本只在私有字体副本中将这些 ID 移到 U+E100–U+E9FF。这个映射只用于字体槽位，源文本与码点保持原值。

## 重建与验证

需要 VS 2022 Build Tools 的 MSVC x64、MASM、Windows SDK、CMake 和 Ninja。本项目已固定 UTFCPP v4.0.8 与 MinHook v1.3.4 的子模块提交。

```powershell
git submodule update --init --recursive
.\tools\build.ps1
.\tools\test-guards.ps1
```

研究压缩包已附带所需依赖源码，直接构建即可；其中的 `build` 目录也附带已验证的 DLL。项目文件夹需命名为 `EU4UnicodePatch`，以符合原型的隔离目录校验。

重新准备隔离游戏副本会读取约 5 GB 本机游戏资源：

```powershell
.\tools\prepare-runtime.ps1
.\tools\prepare-test.ps1
```

运行跟踪需要 Python 和 Frida。本机可使用已有的研究工具环境；跟踪只针对本项目隔离 EXE，结束时关闭该专用实例。交互测试使用 `start-test.ps1`，无需 Frida：

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-import.py --duration 75
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-trace.py
```

## 当前边界

普通文本的闭环已可运行。以下能力尚未完成，不应据此安装到正式游戏或替代整套汉化补丁：

- `§Y…§!` 颜色、`£adm£` 图标等格式标记的 UTF-8 解析；测试文件保留样例，但尚未将其画面计入通过项。
- 地图文字、输入法、光标移动、删除、搜索、存档中的自定义名称和联机文本。
- 完整 Unicode 换行规则、字素簇、组合字符、双向文字及上下文 shaping。
- 动态字体回退与非 BMP 字形。`𠀀`、`😀` 的 UTF-8 原文可完整到达绘制入口，当前 BMFont 适配器显示缺字占位。
- 字体未收录的韩文及其他字符。目前能解析这些码点，不能据此宣称其字形已经可用。

支持的 EXE SHA-256：

```text
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

本 DLL 在游戏初始化前由本机 `version.dll` 加载器加载。字体分配补丁不支持在已运行的游戏中热加载。旧 `plugin64.dll` 会改写重叠位置，不能同时启用。现有旧转义汉化文本也需要独立转换或适配，当前测试夹具只使用直接可读的 UTF-8 文本。

## 后续层次

先补齐格式解析和其他 UI 入口，再覆盖输入编辑和持久化边界。字体接口继续以 32 位码点为输入，扩展动态字体、非 BMP 字形和成熟的 shaping、双向文字与换行处理。该原型的位图槽位映射仅属于字体后端，不成为新的文本编码。

源码布局：`unicode_text.cpp` 管理 UTF-8 与码点；`plugin.cpp` 管理版本校验和引擎边界；`text_hooks.asm` 保存现场并适配不同引擎循环；`tools` 管理隔离测试与证据。

依赖与研究来源见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。游戏、字体、加载器和 DLC 资源不随本项目分发。
