# EU4 Unicode UTF-8 补丁原型

针对 EU4 1.37.5 Windows x64 的隔离研究原型。标准 UTF-8 中文已经通过本地化读取、主文字、普通按钮、颜色、资源图标、中文换行、国家地图标签及实际事件窗口验证。系统字体模式已在游戏中显示扩展汉字“𠀀”、emoji、韩文与希腊文。文本保持标准 UTF-8，不使用旧补丁的转义编码。

当前仍是研究版本，不能据此宣称整套游戏已经支持完整 Unicode。DLL 只接受本项目 `private/runtime/eu4.exe` 测试副本，不会在正式 Steam 游戏目录启用。

## 已验证范围

| 层次 | 当前行为 |
| --- | --- |
| 本地化 | 保留原始 UTF-8；源文件按游戏要求带 UTF-8 BOM |
| 字符遍历 | UTFCPP 解析 1–4 字节字符；32 位 Unicode 标量；缓冲区截断保留完整字符 |
| 原生字形表 | ASCII 保留原生表，U+0100–U+10FFFF 使用稳定指针的稀疏表；不截断码点，不占用私用区 |
| 字体生命周期 | 原生字体表析构释放稀疏记录与旧别名；五种字号的 64 次原生构造/加载/析构、尺寸与地址复用通过 |
| 系统字体图集 | DirectWrite 根据夹具实际字符选择系统回退字体，生成原生 FNT/DDS；𠀀、😀、韩文进入游戏绘制 |
| UI | 主文字、按钮及位图测宽/绘制路径按完整 UTF-8 字符推进 |
| 颜色与图标 | `§Y…§!`、`§G…§!`、`§R…§!` 和 `£adm£/£dip£/£mil£`；中文尾字节不会误触发格式解析 |
| 地图 | 中文国家名测宽、绘制、间距和遍历；补齐第二处顶点容量计数；整包地图 186 个 Unicode 标签的实际顶点未超过分配容量 |
| 汉化迁移 | 审计并转换本机 146 个旧协议 YML 文件，保持源文件与格式；整包菜单、设置、国家面板、战局通过 |
| 存档与路径 | 修复 UTF-16 代理对路径转换及 Load Game 的旧转写；中文/生僻字文件名原生读写，四类名称经压缩 → 非压缩存档保留并重启读取，见 [持久化验收](docs/persistence.md) |
| 换行 | 主文字路径使用 ICU 行边界，避免在字符内部断开及在中文逗号、句号之前断行 |
| 国家搜索 | 外交名单使用 Unicode 大小写/规范/全角匹配，保留既有重音行为和查询原文；12 组实际候选过滤通过，见 [搜索验收](docs/search.md) |
| 输入实验 | 单行编辑、选区和完整 SDL 提交默认关闭；原生像素定位/宽度截断在完整字素边界上执行，11 组、727 个像素位置通过，见 [输入验收](docs/input.md) |
| 隔离保护 | 目录、EXE 哈希、指令字节和旧插件冲突校验；失败时不启用 |
| Unicode 服务 | ICU 字素边界、组合字符/ZWJ 序列处理及 NFKC casefold 搜索键，独立测试通过 |
| 后续字体模块 | DirectWrite 字体回退、复杂文字 shaping、双向文字、测宽及 UTF-8 字素命中测试；独立渲染通过 |

运行证据与限制见 [validation.md](docs/validation.md)。游戏截图：[生僻字与更多语言](docs/evidence/utf8-supplementary.jpg)、[系统字体事件](docs/evidence/utf8-system-font-event.jpg)、[主菜单格式](docs/evidence/utf8-format.jpg)、[中文地图](docs/evidence/utf8-map.jpg)。[复杂文字布局截图](docs/evidence/unicode-layout.png)来自独立测试程序，复杂文字排版尚未接入游戏绘制。

## 本机运行

```powershell
cd D:\Astra-Paradox\repos\EU4UnicodePatch
.\tools\prepare-test.ps1 -SystemFonts
.\tools\start-test.ps1
```

准备脚本只生成私有测试模组与字体副本。测试会修改部分主菜单与国家本地化，并在测试战局开始时触发专用事件。这些夹具会改变游戏校验和；它们不用于铁人成就或联机兼容性验收。测试用户目录与正式存档分离。

修改 [本地化源文件](fixtures/localisation/eu4_unicode_probe_l_english.yml) 后，退出测试实例、重新准备并启动即可。

首次准备约 5 GB 的游戏副本，需要本机已经安装 EU4、现有 x64 `version.dll` 加载器。系统字体模式使用本机字体；不传 `-SystemFonts` 时使用工坊中文字体 `2976470733`：

```powershell
.\tools\prepare-runtime.ps1
.\tools\prepare-test.ps1 -SystemFonts
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

六个 CTest 分别覆盖 UTF-8 核心、ICU Unicode 服务、DirectWrite 布局与栅格化、原生稀疏字形表、编辑事务，以及国家显示名搜索。编辑测试覆盖光标落在 UTF-8/字素内部、完整字素删除、选区替换和字节预算。公共 MASM 宏文件也有显式构建依赖。保护测试会在两个错误宿主中加载 DLL，检查拒绝日志与导出的 `Eu4UnicodeProbeEnabled()` 状态。

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

已实现完整 SDL UTF-8 提交与单行字素移动、删除及选区适配。每个 SDL 文本事件携带完整有效 UTF-8 穿过原生队列，再一次性插入编辑器；预算与控件范围回删保持完整字素，通知只发布最终状态。实际 SDL 轮询入口的 26 个合成事件案例、原生无选区编辑的 18 个案例，以及 18 组选区操作的 27 个状态均已核对；包括中文/生僻字选区替换和禁止提交保留原选区，见 [编辑验收](docs/input.md)。物理键盘中文输入、输入法预编辑、鼠标选区和多行尚未验收。名称的数据往返不能替代输入入口的验收。输入实验默认关闭：

```powershell
.\tools\start-test.ps1 -ExperimentalInput
```

该开关只写入测试副本插件旁的 `eu4_unicode_probe.ini`。独立 ICU 字素测试通过不等于游戏编辑器集成通过。

## 当前限制与完整范围

- 系统字体模式按本地化夹具生成图集，已经显示非 BMP 和新增语言字符；工坊字体模式的覆盖取决于原字体。未收录字符仍使用占位。当前不是运行时按需生成任意字形。
- 图集每字号一页，宽 1024、最高 8192，超过预算会停止生成。字体拥有者销毁与重新创建已验收；活字体原位热重载、运行时缓存与多页纹理桥接仍需完善，见 [字体桥接说明](docs/font-bridge.md)。
- DirectWrite 的 shaping、bidi 与复杂文字选区/命中测试已在独立模块通过，尚需接入游戏统一布局。逐码点图集不能正确排版阿拉伯文等复杂文字。
- 外交国家名单的 Unicode 搜索已接入并验收；其他搜索框仍需逐一接入。地图大小写路径目前保留非 ASCII 字节，只转换 ASCII 字母。
- 物理中文输入/粘贴、鼠标选区、多行、自定义名称往返和联机文本尚未完成验收。单行原生选区函数和合成 SDL 替换已有证据。整包迁移、手动中文文件名、压缩/非压缩格式，以及本地月度自动保存、轮换和重启读取已有运行证据；其他保存入口与长时间回归仍需覆盖。
- 支持版本固定为此 SHA-256 的 EU4 1.37.5 EXE。没有对其他版本或加载中的游戏提供热补丁。

完整预期内容、依赖关系与验收条件见 [roadmap.md](docs/roadmap.md)。仍未完成的项目保留为待办，不以计划或独立测试替代游戏验收。

```text
支持的 eu4.exe SHA-256:
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

旧 `plugin64.dll` 与本补丁修改重叠位置，不能同时启用。旧转义汉化文本可使用已审计的 [迁移工具与流程](docs/migration.md) 转为普通 UTF-8 副本；转换不写入原模组。迁移格式必须明确指定，不根据“能够解码为 UTF-8”猜测文本协议。

源码按关注点分层：`unicode_text` 管理编码与标量，`unicode_services` 管理 Unicode 边界和搜索，`unicode_layout` 管理系统布局与字体，`plugin.cpp` 管理版本与引擎边界，汇编文件适配原生调用现场。依赖与研究来源见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
