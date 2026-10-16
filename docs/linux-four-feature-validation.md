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

## 发布前仍未覆盖的范围

- 本轮 Windows DLL 有 CI/包哈希验证，没有新的 Windows 原生游戏矩阵。
- 更广的编辑控件、密码框、缩放矩阵，缓存淘汰/token 耗尽压力测试仍待完成。
- 实体 GPU、Wayland、其他发行版和旧系统 ABI 未验证。
- Steam 好友端远程显示未验证；accepted=1 只证明真实本地接口交接。
- llvmpipe 地块杂色在此前无补丁对照也复现、softpipe 对照正确，该驱动问题独立存在。

PR 保持 Draft；没有合并、Release、工坊更新、订阅/日常播放集变更，也没有保存游戏。
