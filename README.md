# EU4 Unicode UTF-8 补丁原型

针对 EU4 1.37.5 Windows x64 的隔离研究原型。标准 UTF-8 中文已经通过本地化读取、主文字、普通按钮、颜色、资源图标、中文换行、国家地图标签及实际事件窗口验证。文本保持标准 UTF-8，不使用旧补丁的转义编码。

当前仍是研究版本，不能据此宣称整套游戏已经支持完整 Unicode。DLL 只接受本项目 `private/runtime/eu4.exe` 测试副本，不会在正式 Steam 游戏目录启用。

## 已验证范围

| 层次 | 当前行为 |
| --- | --- |
| 本地化 | 保留原始 UTF-8；源文件按游戏要求带 UTF-8 BOM |
| 字符遍历 | UTFCPP 解析 1–4 字节字符；32 位 Unicode 标量；缓冲区截断保留完整字符 |
| UI | 主文字、按钮及位图测宽/绘制路径按完整 UTF-8 字符推进 |
| 颜色与图标 | `§Y…§!`、`§G…§!`、`§R…§!` 和 `£adm£/£dip£/£mil£`；中文尾字节不会误触发格式解析 |
| 地图 | 中文国家名测宽、绘制、间距和遍历；进入法国战局后正常运行 |
| 换行 | 主文字路径使用 ICU 行边界，避免在字符内部断开及在中文逗号、句号之前断行 |
| 隔离保护 | 目录、EXE 哈希、指令字节和旧插件冲突校验；失败时不启用 |
| Unicode 服务 | ICU 字素边界、组合字符/ZWJ 序列处理及 NFKC casefold 搜索键，独立测试通过 |
| 后续字体模块 | DirectWrite 字体回退、复杂文字 shaping、双向文字、测宽及 UTF-8 字素命中测试；独立渲染通过 |

运行证据与限制见 [validation.md](docs/validation.md)。游戏截图：[主菜单格式](docs/evidence/utf8-format.jpg)、[中文地图](docs/evidence/utf8-map.jpg)、[中文事件](docs/evidence/utf8-event.jpg)。[多语言字体模块截图](docs/evidence/unicode-layout.png)来自独立测试程序，尚未接入游戏绘制。

## 本机运行

```powershell
cd D:\Astra-Paradox\repos\EU4UnicodePatch
.\tools\prepare-test.ps1
.\tools\start-test.ps1
```

准备脚本只生成私有测试模组与字体副本。测试会修改部分主菜单与国家本地化，并在测试战局开始时触发专用事件。这些夹具会改变游戏校验和；它们不用于铁人成就或联机兼容性验收。测试用户目录与正式存档分离。

修改 [本地化源文件](fixtures/localisation/eu4_unicode_probe_l_english.yml) 后，退出测试实例、重新准备并启动即可。

首次准备约 5 GB 的游戏副本，需要本机已经安装 EU4、现有 x64 `version.dll` 加载器和工坊中文字体 `2976470733`：

```powershell
.\tools\prepare-runtime.ps1
.\tools\prepare-test.ps1
```

游戏、DLC、加载器、工坊字体与测试存档不会进入分发包。项目目录名需为 `EU4UnicodePatch`，以满足原型目录校验。

## 构建与检查

需要 PowerShell 7、VS 2022 的 MSVC x64 / MASM / Windows SDK，以及 CMake 和 Ninja。Unicode 服务使用 Windows 自带 `icu.dll`，需要 Windows 10 1903 或更新版本；DirectWrite 模块使用系统字体。

```powershell
git submodule update --init --recursive
.\tools\build.ps1
.\tools\test-guards.ps1
```

构建脚本通过 `vswhere` 查找工具链，实际探测本机编译器的 include 前缀，让 Ninja 正确记录头文件依赖。需要重新配置时传入 `-Fresh`。重新构建前退出测试游戏；游戏的调试组件可能保持 PDB 打开。

三个 CTest 分别覆盖 UTF-8 核心、ICU Unicode 服务和 DirectWrite 布局。保护测试会在两个错误宿主中加载 DLL，检查拒绝日志与导出的 `Eu4UnicodeProbeEnabled()` 状态。

运行跟踪需要 Python、Frida 和 psutil。本机已有研究环境：

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-import.py --duration 75
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-trace.py
```

跟踪脚本部署当前 DLL，记录 EXE / DLL 指纹与真实运行事件；结束时先关闭它启动的专用游戏，再释放代理。普通交互测试使用 `start-test.ps1`，无需 Frida。

独立字体模块可生成诊断图片：

```powershell
.\build\unicode_layout_tests.exe .\private\unicode-layout.png
```

## 实验输入

已实现 SDL UTF-8 提交入口及单行、无选区的字素移动/删除适配，但实际中文输入、选区、多行编辑和持久化尚未验收。输入实验默认关闭：

```powershell
.\tools\start-test.ps1 -ExperimentalInput
```

该开关只写入测试副本插件旁的 `eu4_unicode_probe.ini`。独立 ICU 字素测试通过不等于游戏编辑器集成通过。

## 当前限制与完整范围

- 游戏仍使用本机 BMFont 夹具。非 BMP 字形及未收录的韩文等显示缺字占位，原始 UTF-8 字节保留。
- DirectWrite 的生僻字、更多语言、字体回退、shaping 和 bidi 已有可运行的独立模块，尚需接入游戏测宽、绘制、选区及命中测试。
- ICU 搜索键尚未接入游戏搜索入口；地图大小写路径目前保留非 ASCII 字节，只转换 ASCII 字母。
- 中文输入/粘贴、编辑选区、多行、中文存档文件名、自定义名称往返、联机文本和完整汉化模组迁移均未完成验收。
- 支持版本固定为此 SHA-256 的 EU4 1.37.5 EXE。没有对其他版本或加载中的游戏提供热补丁。

完整预期内容、依赖关系与验收条件见 [roadmap.md](docs/roadmap.md)。仍未完成的项目保留为待办，不以计划或独立测试替代游戏验收。

```text
支持的 eu4.exe SHA-256:
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

旧 `plugin64.dll` 与本补丁修改重叠位置，不能同时启用。旧转义汉化文本需要基于真实源格式单独转换，本测试夹具使用直接可读的 UTF-8。

源码按关注点分层：`unicode_text` 管理编码与标量，`unicode_services` 管理 Unicode 边界和搜索，`unicode_layout` 管理系统布局与字体，`plugin.cpp` 管理版本与引擎边界，汇编文件适配原生调用现场。依赖与研究来源见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
