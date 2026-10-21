# Linux 四组功能收尾验收（2026-10-17）

四组功能的主路径已经实现，并完成下述原生游戏验收。结论限定在已调查的 Steam EU4 1.37.5 Linux x86_64、Arch、X11/Fcitx5 环境；这不等于 Linux 发布矩阵全部通过。

功能提交：`f4d3e523803263560233eebd5ad413dba339ddca`。CI **38973692833** 成功，Windows **27/27**、Arch **18/18**。C++ 产物均来自 CI，没有在本机编译。最后三个修复只修改 Linux 适配代码。

## 实际缺陷与修改位置

1. **地图区域返回值丢失**：`CGenerateNamesWork::Update` 在 `0x1b60a75` 读取 `AddNameArea` 返回的 AL，累计成功标签后裁切容器。新包装器曾以 `void` 调用并返回，导致生成的区域被裁掉。`map_names.cpp` 的包装器现按原生 ABI 返回 `bool`，保留真实结果。国家标签从旧产物的 0 项恢复到 710 项，最终观察到 56,117 次地图分页索引绘制，退出日志为 56,124 次。
2. **姓名策略使用显示译名**：`CCulture::GetName()` 返回 `+0x60` 的本地化名称。构造函数 `0xf4e168` 把脚本标识存入 `+0x40`，`0xf4e254` 再用该标识生成显示名称。`name_presence.cpp` 改用 `+0x40`，让内置文化策略和 `EU4_UNICODE_NAME_<culture>` 配置匹配稳定 ID，并增加两处原生签名校验。
3. **外交查询先被 Latin-1 清理破坏**：`OnFilterOpinions` 在结果匹配前调用 `ToLower`、`CleanForSorting`。`search.cpp` 将返回点 `0x1a87fc9`、`0x1a87fd1` 加入 UTF-8 查询规范化范围，并校验调用位置。其他 CString 调用方保持原行为。

所有位置均按完整 ELF 哈希、函数边界和实际指令复核。没有重命名模组或对象、引入名称别名，或写死工坊 ID。

## 最终原生测试

`four-final-f4d3e52`，PID **22992**，独立 runtime/userdir。测试直接启动 Linux 原生 ELF，并连接专用离线 Steam；不是 Proton，也不是 Steam 客户端派生启动。

| 功能 | 实际结果 |
|---|---|
| IME | 外交框显示 `ce shi` 行内预编辑及 Fcitx5 候选，真实文档保持为空；选词后真实文档和原生外交查询均为“测试”。 |
| 双向选区 | 文档 `A سلام B`，逻辑选中 `A س`；实际绘制区域 `[0,11]`、`[11,16]`、`[34,47]` 保留中间间隙，绘制计数增加，GL 状态检查 32 次、错误 0。 |
| 地图与字体 | 国家/省份容器均有有效记录，地图分页索引绘制实际执行。国家画面显示 Arabic/Hindi 与原有中文标签；原生查找器定位省份 1 后，地图显示连写的 `سلام ستوكهولم`，邻近中文地名正常。微调省份路径实际生成 8 次复杂标签。 |
| 外交查询 | `勃艮第`、`BoGenDi`、`bgd` 均在真实外交列表筛出勃艮第；`zzzznonexistent` 清空结果。原生 `view+0x1210` 快照核对了规范化查询，截图核对了结果条目。 |
| 查找器 | 阿拉伯文完整查询只返回省份 1；真实 CGotoBox vector 的 ID、显示名、规范化字段一致，点击条目后镜头正确跳转。 |
| 姓名 | 共 571 次策略格式化。日志记录 `hungarian`、`japanese` 等稳定文化 ID。普通控制台生成明国将领后，原生 CCountry/CList/CLeader 中读到“熊绍宗”；原始表有名 `绍宗 #0` 和姓 `熊`，测试文化标识为 `native_closeout`，显示译名为 `Native Closeout`。 |
| Steam 状态 | 实际 SetRichPresence `country` 载荷为 19 字节有效 UTF-8，accepted=1，哈希与测试国名 `سلام فرنسا` 一致。账户原文不写日志。 |
| 原版汉化 | setup.log 为 `rival=宿敌`、`enemy=敌对关系`，无主汉化地图字库缺失错误；使用已有本地汉化资源，没有下载工坊。 |

## 已有独立证据的归属

`d018f69` 引入地图 cluster、IME 整行视口和多区域选区，其 SO 为 `14c406e32f15cb52c965d1c0c9c8782e525d7e9f6a94ee601850a8d1f491ed67`。保留该产物的长预编辑原生证据：完整 `A سلام B ` 行先 shape，再裁切宽 159 的视口、左偏移 114；组合中点击安全取消，真实文档保持完整。

该旧产物还通过九组真实查找器矩阵：中文、全拼、首字母、繁简、英文、大小写、混合查询和无结果。`bj` 只证明包含北京，不代表北京排名第一；非精确 `beijing` 独立验证了北京排在贝宁之前。旧证据没有重新归属最终 SO。其地图/姓名失败记录、失败 CI 及输入诊断也原样保留。

`77b0d47` 的 softpipe 对照因渲染过慢提前结束，退出 0、默认目录未变，不计完整功能验收。30 分钟观察器的超时结果没有被重写；最终成功退出另存 `post-harness-exit.json`。

## 固定产物与证据路径

```text
Linux ELF: af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3
Linux SO:  c064d059f0f8426f6bac761bc9174b1bdde848315a3a3db4738e9ebdee167f0d
Windows eu4_unicode_patch.dll:
3e63ec7995d401419d544f2279b6221cd37e48c69b2d5d546e95644654b3e9a1
Windows VERSION.dll:
d8d7cde7349f240f4284bb0adb0f99cc0309da342011665aff496aec0812b682
```

证据根目录：`D:/Astra-Paradox/artifacts/linux-four-features-2026-10-15/`。原始测试目录：`D:/Astra-Paradox/artifacts/linux-native-arch-2026-10-14/native-implementation/tests/`。

- `native-four-feature-closeout.json`：审计结果、产物哈希、原生计数和限制。
- `ci-final-f4d3e52.log/.json`、`ci-38973692833/`：CI 日志及两平台构建产物。
- `native-country-map-f4d3e52.png`、`native-nudged-map-f4d3e52.png`：真实地图。
- `native-ime-preedit-f4d3e52.png`、`native-bidi-selection-f4d3e52.png`：实际编辑控件。
- `native-diplomacy-{chinese,pinyin,initials,absent}-f4d3e52.png`：外交四组结果。
- `final-four-feature-cleanup.json`、`final-verified-assets.json`：退出、默认目录、配置及源资源校验。
- `tests/four-final-f4d3e52/post-exit/`：完整原始日志、描述符、启用列表和退出归档。
- `environment/verify-four-feature-closeout.py`：只读审计器，检查真实原生记录和产物，没有复制匹配逻辑来冒充游戏测试。

最终仅结束本任务的游戏，退出码 0。专用 Steam 服务及输入桥已停止；默认用户目录内容和 mtime 完全一致，夹具源资源哈希一致。启动项清理保护曾拒绝覆盖，随后确认当前 EU4 启动选项已经为空，以只读核验保留当前 VDF 字节。

## 最终 Windows 原生回归（2026-10-17）

在同一 CI **38973692833** 生成的 `EU4UnicodePatch-1.37.5-v0.1.14.zip` 上，用当前 Windows Steam EU4 1.37.5（`release_1.37.5`，revision `835bfdf8ca24c291a1b3f1b5bc72d47e7df1ae18`）重新运行了 14 组真实原生登记和资源加载夹具。游戏 EXE SHA256 为 `9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a`；本次实际映射的 `eu4_unicode_patch.dll` SHA256 为 `3e63ec7995d401419d544f2279b6221cd37e48c69b2d5d546e95644654b3e9a1`，`VERSION.dll` 为 `d8d7cde7349f240f4284bb0adb0f99cc0309da342011665aff496aec0812b682`。每组都使用独立 userdir，运行器只读采集原生进程快照，没有 Frida 或远程写入。

14/14 组进入主菜单，14/14 组确认补丁 DLL 已加载，0 组出现修复错误。唯一启用的 UTF-8 候选在旧工坊登记或本地副本登记先后两种顺序下都被选中；中文外交文本 `宿敌`、`敌对关系` 和 `gfx/fonts/zh-hans-map` 在 12 个应加载中文资源的组中均实际出现。依赖探针的正反启用列表仍按原始名称 `Chinese Language Mod for 1.37` 匹配并覆盖 `宿敌（依赖覆盖验证）`。ZIP 形态的主汉化也分别覆盖了未启用候选先出现和已启用候选先出现两种路径。

全未启用组保留原生首项并保持英文及地图字库缺失；两个同时启用组和“未启用项先出现、两个启用项随后出现”组均保留原生首项，并记录 `native-multiple-enabled` 冲突。这是本次限定语义的预期结果，不扩展为同时加载多份同名模组。

详细证据见 `D:/Astra-Paradox/artifacts/windows-regression-2026-10-17/windows-regression-report.md`、`windows-regression-report.json`、`comparison.json`、`sources-before.json`，以及每组的 `launch.json`、`native-state.json`、`setup.log`、`error.log` 和 `eu4_unicode_patch.log`。原生进程已全部退出，未留下本任务启动的 EU4 进程。

另外复跑了 CI Windows 构建目录中的 10 个可执行共享核心测试和 `executable_check.exe`。EXE 的 354 处代码/数据检查通过；玩家字库 D3D9、分页 GPU、IME、模组登记、姓名顺序、段落、旧文本和格式化测试通过。`native_font_atlas_tests --system-only` 因本机系统字体已经覆盖测试用 U+323B0 而不满足“系统缺字”的测试前置条件，返回 1；同一测试的 `--player` 模式通过，原始日志保留在 `D:/Astra-Paradox/artifacts/windows-regression-2026-10-17/ci-local-rerun/`。

同一最终 DLL 还通过了无游戏入口的原生布局和导入回归：16 个宽度用例、1,589 个布局用例、11 个截断退出、6 个 UI 页面发射、6 个几何范围、4 个按钮格式参数、4 个地图标签适配、7 个多行编辑器，以及 8 个原生脚本词法导入值全部通过。测试映射当前 EXE、校验 354 处原生站点并使用 CI linker map，没有启动游戏或注入 Frida。

用户随后明确允许注入后，又在最终 DLL 上完成了两次真实 Windows 进程观察。路径回归对 ASCII、中文、补充平面、Emoji、U+10FFFF、5 字节容量和 10 个高位/大小写比较方向全部通过；启动导入回归实际捕获 `FE_SINGLE_PLAYER`、`FE_MULTI_PLAYER`、`FE_TUTORIAL` 三个中文键的导入和注册 UTF-8 字节，脚本错误为 0。证据为 `D:/Astra-Paradox/artifacts/windows-regression-2026-10-17/private/path-trace.jsonl` 和 `import-trace.jsonl`；两个被附加的游戏进程均由测试器结束。

## 发布前仍未覆盖的范围

- 本轮 Windows 已完成 14 组模组登记、原始名称依赖和资源挂载原生回归；四组 Linux 专属输入、段落和外交交互的 Windows UI 操作矩阵仍未覆盖。
- Windows 共享核心二次回归中仅有系统字体缺字对照因本机字体覆盖而不满足前置条件；这不代表补丁功能失败，换用具有相同字体前置条件的 CI/隔离环境后才能把该对照记为通过。
- 更广的编辑控件、密码框、缩放矩阵，缓存淘汰/token 耗尽压力测试仍待完成。
- 实体 GPU、Wayland、其他发行版和旧系统 ABI 未验证。
- Steam 好友端远程显示未验证；accepted=1 只证明真实本地接口交接。
- llvmpipe 地块杂色在此前无补丁对照也复现、softpipe 对照正确，该驱动问题独立存在。

PR 保持 Draft；没有合并、Release、工坊更新、订阅/日常播放集变更，也没有保存游戏。
