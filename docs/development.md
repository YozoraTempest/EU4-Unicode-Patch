# 开发说明

补丁针对已校验的 EU4 1.37.5.0 x64。文本存储为标准 UTF-8，引擎字符串与光标位置使用字节偏移。

Windows 与 Linux 的核心源码和构建定义共用，平台依赖及原生入口分别配置，见[共用结构与支持状态](platforms.md)。本文的 RVA、DirectWrite、D3D9 与 IMM 接入描述属于现有 Windows 实现。

Windows 与 Linux 的本地化 YAML 和文本脚本均接受无 BOM 或带 BOM 的 UTF-8。本地化由共享 `yaml_parse_buffer` 判断编码，在各平台原生解析入口借用临时带签名缓冲区；不改写模组文件。脚本在初始化时跳过输入开头的 BOM，保留正文中的 `U+FEFF`。旧 52 本地化及脚本文本继续分别按 CP1252 包装转义和原始双字节转义导入。

## 模块

| 模块 | 职责 |
| --- | --- |
| `unicode_text` / `unicode_services` | UTFCPP 编解码，ICU 字素、行边界与搜索键 |
| `unicode_editor` / `editor_document` / `native_editor_text` | 字素编辑、多行位置映射、撤销历史与字节预算 |
| `unicode_search` / `unicode_pinyin` / `native_search` | 中文与拼音匹配、词组读音及国家／省份搜索适配 |
| `yaml_input` / `native_yaml` | 共享 YAML 签名规则及 Windows 原生解析器缓冲区适配 |
| `native_script_bom` | 脚本输入的 UTF-8 BOM 识别与解析器初始化 |
| `legacy_text` / `native_legacy_import` | EU4dll 旧转义解码、本地化注册与脚本文本导入 |
| `native_name_order` | 分开的姓／名按人物文化拼接，保留旧姓氏标记与模组规则 |
| `native_mod_registry` | 同名描述符的启用状态检查与原生模组登记修正 |
| `unicode_layout` | DirectWrite 字体集合、布局与栅格化 |
| `shaped_paragraph` / `formatted_paragraph` / `native_paragraph` | 整段塑形、颜色及图标映射、原生绘制调用适配 |
| `native_editor_selection` | 复杂文字选区的原生矩形及控件生命周期 |
| `native_ime` / `native_editor_presentation` | 输入法候选窗、UTF-16 预编辑读取与临时绘制文本 |
| `glyph_registry` / `scalar_glyph` / `native_font_atlas` | 稀疏字形记录、按需图集和设备恢复 |
| `font_assets` / `font_atlas_assets` | 原版字体路径映射与运行时基础图集生成 |
| `plugin.cpp` / MASM | 指令检查、引擎挂钩与失败回滚 |
| `executable_compatibility` / `eu4_1375_profile` | 映像布局、必要代码与内存范围检查 |
| `version_proxy` | Windows version API 转发及插件加载 |

`eu4_unicode_patch.dll` 用于玩家目录，输入默认开启；`eu4_unicode_probe.dll` 保留隔离目录和输入开关，供原生探针使用。

EXE SHA-256 只记录到日志。加载前检查实际映像的 x64 PE 布局、必要代码与常量，以及全局地址的内存权限；允许不重叠的新增节和无关修改。挂钩创建后、首次写入前再次检查，失败或异常时清理本次挂钩并恢复已写入的常量。`executable_check.exe <eu4.exe>` 可映射本机 EXE 做同样的只读检查，不执行游戏入口。

## 引擎入口

| 路径 | RVA |
| --- | --- |
| 字体加载 / 字体表析构 | `15953c0` / `1594360` |
| 纹理查找 | `16c3f10` |
| 编辑按键 / 插入 | `15366c0` / `1536b80` |
| 编辑行缓存 / 绝对位置 | `15373a0` / `1536f50` |
| Windows IME 消息 / 光标矩形 | `1764940` / `17657c0` |

ASCII 保留 256 槽表，其他标量进入稳定的稀疏记录。字体路径通过游戏的字符串赋值函数替换，使用原生分配器；原版同名路径先查询引擎资源解析器（RVA `19fad70`）；`.fnt`、`.tga` 或 `.dds` 来自模组或压缩包时保留原路径。自定义路径保持原生加载，普通与放大 UI 路径分别处理。

## 模组登记

EU4 1.37.5 的模组表以原始 `name` 为键。原生初始化先扫描 `.mod` 并丢弃同名条目，再读取 `dlc_load.json` 和填充启用列表。`native_mod_registry` 先执行原生插入，仅在不同描述符发生同名碰撞后记录候选；第一次登记的路径从返回节点读取，后续候选从未被消耗的临时对象读取。无重名的模组不分配逐项元数据。初始化结束后才检查启用状态。独立重扫描在自身结束时检查当前启用列表，不保存跨调用的 manager、树节点或描述符指针。本版本已确认初始化只调用一次非递归扫描；独立扫描入口的生命周期另有自动测试。

同名候选恰好一份启用时，若原生登记丢弃了它，使用原生解析函数重新读取该描述符，并通过原生深赋值函数更新现有节点的对象。重解析只允许修改已捕获的节点；名称、对象名称或完整路径改变时拒绝候选，不调用树插入。返回已有节点和 `inserted=false`，与原生重复插入的结果一致，临时对象仍走解析器的正常析构。字符串、依赖容器和其他成员仍由引擎分配、赋值和析构；树键与对象名称保持原值。路径检查使用原生收集启用模组所用的完整长度／字节匹配函数。全部未启用或多份同时启用时保留原结果；多份同时启用不提供并行加载支持。

冲突日志记录候选、启用状态、期望选择、实际选择与处理状态。日志构造或输出失败不终止其他组的处理。原生解析／赋值异常则停止后续修复；深赋值没有经过验证的回滚保证，日志将该节点状态记为未知，不声称旧对象完整保留。

拦截树插入时必须同时处于该 manager 的模组扫描或选定描述符的重解析上下文，且容器地址必须等于 `manager+0xd0`。DLC 表和其他调用直接转发。无同名冲突不重解析、不赋值、不输出逐项日志。六个相关原生函数的完整字节加入映像预检，挂钩沿用统一创建、启用和失败回滚流程。

原生挂钩生命周期的自动检查位于 `native_mod_registry_tests`；真实游戏的顺序与资源加载验证由隔离启动测试完成，不能由模拟对象测试替代。

`tools/verify-mod-registry.py` 创建新的隔离 runtime、私有可写字体目录和每组独立 userdir，使用指定的 CI 玩家包逐次启动游戏。`prepare` 要求空的证据目录及当前有效的工坊／本地上传内容路径；原始内容只读，测试描述符独立生成。`run` 只用 Windows 只读进程 API 检查自己启动的游戏，并保留原生登记表、名称依赖、原版中文文本、地图字体日志、描述符、启用列表及文件哈希。脚本不注入、不调用远程函数、不修改当前播放集，只终止自身的测试进程。

```powershell
python tools/verify-mod-registry.py prepare --root D:/evidence/new-mod-registry-test --game "D:/SteamLibrary/steamapps/common/Europa Universalis IV" --workshop D:/SteamLibrary/steamapps/workshop/content/236850 --upload D:/path/to/current/local-upload --settings D:/path/to/test/settings.txt
python tools/verify-mod-registry.py run --root D:/evidence/new-mod-registry-test --case old-disabled-first --label after --package D:/ci/EU4UnicodePatch-1.37.5-v0.1.14.zip
python tools/verify-mod-registry.py compare --root D:/evidence/new-mod-registry-test --before before --after after
```

样例包含无冲突、旧工坊／本地上传及压缩包的双向扫描顺序、三候选唯一启用、全部未启用和多份同时启用。隔离描述符统一引用测试中的实际名称，比较时检查依赖目标存在。依赖探针按主汉化旧名称声明依赖，并覆盖相同路径的本地化文件：正反启用列表均应输出 `宿敌（依赖覆盖验证）`，去掉依赖的对照应输出 `宿敌`。这通过原生资源覆盖结果检验加载顺序。

对修复前后的 CI 包运行相同样例，检查结果与原生日志；若对照包已包含碰撞修复，给 `compare` 加 `--before-fixed`。历史描述符路径不能直接复用。快照使用 schema 2，并记录采集脚本哈希；原生对象 `+0xb0` 为目录、`+0xd0` 为压缩包。2026-10-12 的旧 baseline 快照曾将这两个输出字段对调，原始日志与描述符保持不变，应按更正说明解释历史字段。此游戏验证需要拥有对应本机内容，不在公共 CI 上运行。

## 字体

玩家版按需字形先用系统 DirectWrite 字体；缺字后才加载游戏目录中的可选字体文件。开发探针保留文件字体优先的验证方式。安装或移除字体包后需重启游戏。

基础图集在原生字体加载回调中从系统字体生成，共有 14、16、18、24、88px 五种字号，每种初始一页，大小为 2048×4096，包含 192 个基础字符。UI 与地图图集均按需分页。同一进程重复加载时复用，下一次启动重新生成，缓存位于 `gfx/fonts/eu4-unicode/cache/`；DirectWrite 初始化在 DLL 加载锁之外执行。可选字体的版本与 SHA-256 见[清单](../fixtures/open-fonts.json)，版权与许可证见[第三方说明](../THIRD_PARTY_NOTICES.md)。

模组位图字体保留原图集整页，缺字写入独立尺寸的补充页，每个维度最多 2048 像素；原图较小时取其对应维度。已有普通字形以原字宽参与混排，组合字与连写文字由 DirectWrite 塑形；原图集不重排、不上传覆盖。绘制按各页尺寸换算 UV，补充页沿用现有设备重置链路。

测宽阶段只生成 CPU 字形，纹理查找阶段上传，旧坐标和 UV 保留。共享纹理的字体共用图集，Reset 后从 CPU staging 恢复。原生纹理只作非持有引用，补丁的分页纹理与绘制缓冲在 Reset 前释放；托管顶点缓冲保留缓存，临时缓冲在原生释放入口清理。

2048×4096 的基础图集约占 32 MiB GPU 内存；2048×2048 的补充页约占 16 MiB，上传缓冲另占 16 MiB CPU 内存。模组原图只作引用，其 RGBA 尺寸估算与补丁新增纹理分别统计，不计入新增纹理预算。每个图集最多 8 页，补丁持有的纹理预算为 256 MiB，顶点缓存上限为 64 MiB。达到容量上限或字体缺字时仍可能显示占位符。

每个图集的排版缓存预算为 8 MiB，按最近使用顺序移除未被调用方持有的布局和字形记录，并回收绘制令牌。图集像素和 UV 保持稳定，以保留游戏已缓存的顶点。排版缓存预算按文本、字素和光标位置的估算成本计费。

UI 绘制、测宽和换行共用 DirectWrite 布局；颜色标记不切断塑形，国旗、点数、货币及数字图标作为内嵌对象参与双向排列。主绘制与按钮入口保留原生图标命令；不支持这些命令的 popup 入口保持原绘制和测量契约。编辑将 UTF-8 字节位置映射到行、视觉光标和选区。地图国名与省份名使用整段塑形后的字形簇，保留原生领土适配与曲线布局；连写文字不插入原生字间填充空格。

绘制令牌仅存在于当前绘制调用，原始本地化、编辑文本和存档保留 UTF-8。

字库 cmap 覆盖审计使用 `tools/audit-open-fonts.py`，依赖见 `tools/requirements-font-audit.txt`。

## 输入与保存

两平台均支持 `Ctrl+A` 全选当前编辑控件的已提交文本；输入法预编辑时该键由输入法状态处理。编辑通过 ICU 字素边界处理光标与选区。SDL 2.0.4 文本事件载荷最多 31 字节；每个提交保留完整 UTF-8，在原生队列中完成一次插入和通知。

搜索接入外交国家列表与省份查找，支持中文、全拼、首字母、部分拼音、混合输入及简繁匹配。名称按当前显示内容建立索引，词组读音来自固定版本的 `phrase-pinyin-data`，其余汉字由系统 ICU 转写。

至少 5 个字母的完整拼音默认容忍一次插入、删除、替换或相邻字母颠倒；首字母和中文查询不放宽。需要模糊音时，可创建 `plugins/eu4_unicode_patch/config.ini`，重启游戏后生效：

```ini
[search]
typo_tolerance=1
fuzzy_pinyin=1
```

两平台使用同一个 `search_resources` 解析器，配置支持 UTF-8 和 UTF-8 BOM；为保留 Windows 已有配置，带 BOM 的 UTF-16LE INI 也继续接受。`typo_tolerance` 默认 `1`，`fuzzy_pinyin` 默认 `0`。模糊音支持 `zh/z`、`ch/c`、`sh/s`、`n/l`、`en/eng` 和 `in/ing`；设为 `0` 可分别关闭。

特殊读音可写入游戏目录的 `plugins/eu4_unicode_patch/pinyin.txt`，支持 UTF-8 和 UTF-8 BOM，重启游戏后生效。每个汉字对应一个拼音音节，重复词组可添加不同读音，`#` 开头为注释：

```text
奥地利: ao di li
长安: chang an
西藏: xi zang
```

多行位置映射区分原生缓存的三种行尾：实际换行、插入的软换行和替换空格的软换行；末尾换行保留空行光标。撤销历史按一次提交或删除记录，最多 128 次、2 MiB，控件销毁时释放；外部代码替换文本后清除过期历史。

输入法预编辑从 IMM 的 UTF-16 文本读取，只在绘制期间交换显示文本，返回前恢复已提交文本、选区和行缓存状态。提交继续由 SDL 处理，避免预编辑进入搜索、存档或撤销记录。`Esc` 取消预编辑并释放输入框焦点，`Ctrl+Z` 撤销，`Ctrl+Shift+Z` 重做；换行许可保持控件原有设置。

保存路径修正代理对转换与比较，并保留已观察保存入口的 UTF-8 名称。

## 姓名顺序

分开的姓与名在原生姓名拼接入口处理，使用人物自身的文化，不按所属国家改变顺序。汉文化、日文和韩文姓名默认姓在前：原文字形通常连写，拉丁字母转写加空格；匈牙利姓名默认姓在前并加空格。未知文化保持原顺序。旧 `¿` 姓氏标记仍优先决定姓在前，源文本附带的分隔符保留。

模组可在本地化中覆盖文化规则。例如在 `localisation/replace/name_order_l_english.yml` 中使用当前语言标签：

```yaml
l_english:
 EU4_UNICODE_NAME_hungarian:0 "surname_first middle_dot"
 EU4_UNICODE_NAME_japanese:0 "surname_first auto"
 EU4_UNICODE_NAME_custom_culture:0 "given_first space"
```

键名后缀是 `common/cultures` 中的文化 ID。顺序可选 `surname_first`、`given_first`；分隔符可选 `auto`、`none`、`space`、`middle_dot`。规则按游戏当前语言和本地化覆盖顺序读取；旧 `¿` 标记只覆盖顺序。修改后重启游戏。

君主、继承人和配偶在显示时拼接姓名；商人、外交官、传教士、随机顾问和使用同一入口的将领在生成姓名时处理。生成后的完整姓名由游戏正常保存和读取。历史数据或旧存档中已经合成的完整姓名不按空格拆分，也不自动倒置。

## 旧汉化迁移

运行时在本地化注册前解码 UTF-8 中的 CP1252 转义字符，在 GUI／脚本词法入口解码原始转义字节。两处共用 `0x10`～`0x13` 协议解码器，处理旧字符重映射和 UTF-16 代理对；普通 UTF-8 和非转义私用区字符保持原值。搜索、编辑和绘制继续接收标准 UTF-8。

损坏的本地化值不注册，日志记录键名、行号和字节位置；损坏或超出原生缓冲区的脚本文本终止当前文件解析。退出时汇总转换数量。不会改写模组文件。

离线迁移工具仍可用于制作独立的 UTF-8 汉化包：

```powershell
python tools\migrate-localisation.py '旧模组的localisation目录' private\migrated-localisation --format eu4dll-escaped
```

工具只转换 `.yml` 本地化，保留 BOM、换行和文件结构，拒绝截断转义与孤立代理项，输出逐文件哈希报告。玩家直接使用旧包时不需要运行此工具。

## 开发副本

准备开发探针：

```powershell
.\tools\prepare-runtime.ps1 -GameDirectory '游戏目录'
.\tools\prepare-test.ps1 -GameDirectory '游戏目录' -OpenFonts
.\tools\start-test.ps1 -ExperimentalInput
```

准备普通目录的玩家覆盖测试：

```powershell
.\tools\stage-player.ps1
.\tools\prepare-player-test.ps1 -GameDirectory '游戏目录' -SaveFile '测试存档.eu4'
```

玩家副本位于 `private/player-install/Europa Universalis IV`，独立用户目录为 `private/player-userdir`。脚本保留原版字体定义和原安装的旧插件，覆盖玩家包，不修改原安装。`-SaveFile` 可省略。EU4 的 `userdir.txt` 路径不要带尾部换行。

默认测试无可选字体的主包；加上 `-OptionalFonts` 可测试字体包安装后的行为。

`python tools/verify-mod-fonts.py` 在该副本中生成目录及压缩包测试模组，检查同名覆盖、自定义路径、纹理覆盖和放大字体的度量与实际 GPU 像素；脚本退出时恢复副本的模组配置。生成的系统字体纹理仅留在 `private/`。

## 原生探针

Frida 探针需要独立 Python 环境、Frida 和 psutil；剪贴板探针另需 pefile。MAP 必须与当前 DLL 一致。结束调试前先关闭专用游戏实例。

`tools/trace-*.py` 跟踪原生调用，`tools/verify-*.py` 检查编辑、绘制和设备行为。

动态 GPU 探针在外交搜索框聚焦后使用 `private/dynamic-font-arm.txt` 开始、`private/dynamic-font-finish.txt` 结束。`--player` 改为验证普通目录中的正式 DLL。
