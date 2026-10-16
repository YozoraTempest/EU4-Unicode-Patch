# Arch 原生游戏验证记录

记录日期：2026-10-16（Asia/Shanghai）。目标是 Steam EU4 1.37.5.0 Inca Linux x86_64 原生游戏，基准环境为 Arch Linux；没有使用 Proton。Windows/Linux 共用 Unicode 核心实现、版本及项目，分别构建 DLL/SO。下列记录按功能树区分，后续实测不能反向归属于较早或较新的二进制。

## 基线源码、CI及哈希

最新功能源码为 `cb15e8c4f7c565d1f1c6059ccc097baf9cad843d`，在启动器55fa3a1的基础上修复多人界面的Unicode昵称。CI **38704238114** 两平台成功：Windows26项、Arch10项CTest，零失败；CI合并提交 `dfe2433a6533f05e571e91de35309a60a65a2783`。全部C++由CI构建，最终入口测试还在完整VM重放通过。

| 文件或产物 | SHA256 |
| --- | --- |
| Linux游戏ELF，Steam build15918133 | `af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3` |
| 最新Linux SO | `cfb27c8182775c5c9e4014f3f9205abfc55fcc6ccf98fe0ad53c7eed77ebc093` |
| 包内启动器 | `d7e558ebcad2e0882f4111e16ec27ef3e3a292c6af944a2cd4613e7b22e49a9d` |
| Windows游戏exe | `9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a` |
| 完成14组游戏回归的Windows DLL，CI38658319276 | `1775c8077fdabb21a4001f9c07c0191000165ebccaeda6aa738a9a17e61a3d67` |
| 最新CI38704238114 Windows DLL，CI26项通过 | `26272cd5c8d963616f0e9fb41bcb4639a5e4759463bb8ade8d574f7f191145f6` |

最后两次功能修改仅涉及Linux适配器及启动器，Windows游戏矩阵未重复运行；Windows14组记录明确对应上表前一批DLL。核心9个程序的完整VM重放也保留为原生核心阶段证据。

## 真实原生登记与资源回归

新SO完整重跑16组，原始结果16/16通过。每组有独立runtime配置及userdir，进入菜单后保留30秒，检查真实模块映射、只读原生树、原始名称、描述符路径、dependencies、setup.log和地图字库日志；没有只测复制的选择函数。矩阵进程受控终止，退出码为null；正常退出0另由下述两轮Steam场景证明。五组无插件对照继续证明原生冲突以及不应改变的选择。

| 用例 | 实际本地化文本 | 结果 |
| --- | --- | --- |
| vanilla | Rival / Hostile relations | 通过 |
| unique | 宿敌 / 敌对关系 | 通过 |
| old-disabled-first | 宿敌 / 敌对关系 | 通过 |
| old-enabled-first | 宿敌 / 敌对关系 | 通过 |
| local-disabled-first | 宿敌 / 敌对关系 | 通过 |
| local-enabled-first | 宿敌 / 敌对关系 | 通过 |
| none-enabled | Rival / Hostile relations | 通过 |
| multiple-enabled | OLD_NATIVE_FIRST / Hostile relations | 通过 |
| multiple-enabled-inactive-first | Rival / Hostile relations | 通过 |
| dependency-forward | 宿敌（依赖覆盖验证） / 敌对关系 | 通过 |
| dependency-reversed | 宿敌（依赖覆盖验证） / 敌对关系 | 通过 |
| dependency-control | 宿敌 / 敌对关系 | 通过 |
| path-backslash | Rival / Hostile relations | 通过 |
| path-absolute | Rival / Hostile relations | 通过 |
| archive-disabled-first | 宿敌 / 敌对关系 | 通过 |
| archive-enabled-first | 宿敌 / 敌对关系 | 通过 |


依赖三组仅启用主汉化和ZZZ探针，补充包登记但未启用，覆盖同一diplomacy_l_english.yml。正向和反向列表声明旧名称依赖时都得到“宿敌（依赖覆盖验证）”；移除依赖则为“宿敌”。树键、name、原生dependencies/rank/排序不变。未启用候选先扫描或后扫描均正确；没有启用或多个启用时保留原生行为并报告冲突。

path-backslash和path-absolute故意提供与扫描登记路径不同的启用字符串；原生资源加载FindIndex未匹配，补丁同样保持未加载及原版英文。它们验证沿用实际路径匹配语义，没有自行规范化、按文件名片段或包含关系判断。多份同时启用的两组也刻意保留原生结果，不能把它们的“通过”理解为支持多份同名模组一起加载。

工坊形态的冲突候选使用真实字段和小型自有标记资源。UTF-8主汉化、补充包、字库与ZIP来自本机已有实际素材。Linux影子候选不是完整旧工坊载荷；Windows14组使用完整旧资源。没有在VM下载工坊内容。早期夹具失配与所有修复前日志都保留，最终结果来自新SO独立16组。

## 实际Steam启动、昵称、地图和退出

最新包通过实际Steam客户端启动，PID1985，SteamAPI_Init加载steamclient成功，64位Overlay库同时映射。菜单、地图国家名及长段落显示中文；本地化导入253544条、拒绝0条，原始49049728字节地图DDS加载为6400×7664。该轮运行1475.68秒，正常退出0，默认userdir未变，最新包的地图与退出证据已经补齐。

多人视图的原生清理曾把Steam UTF-8昵称逐字节当Latin-1转写。新模块只改Show中的两个清理调用点，按Unicode标量替换@/$/§，保留其他Unicode；原生引号/斜杠替换与非UTF-8路径保留。真实playername缓冲区验证为9个Unicode字符、27个字节，有效UTF-8。证据只保存长度、有效性与哈希，没有保存账户文本。

最新SO另完成真实输入场景，PID3536，运行1577.51秒，正常退出0。通过普通noVNC键盘使用Fcitx5拼音ceshi和空格提交，原生CTextBuffer对象记录player测试，完整UTF-8字节706c61796572e6b58be8af95、字节列12；左移至9，右移回12；整字删除后player、列6。四份快照来自同一原生缓冲区，每份都核对exe及实际映射SO的哈希。中文地图、253544条本地化、6400×7664地图字库、SteamAPI及64位Overlay共存再次通过。测试wrapper没有设置LC_ALL，包内启动器自动把Steam的C字符区域转换成C.UTF-8。默认userdir内容和mtime未变。第一次新SO昵称场景的桌面工具键盘未到达VM仍按未验证记录保留；最新输入成功结论来自独立第二轮及input-cb15e8c-validation.json。前一轮55fa3a1包的输入和外部中断记录保留为历史。

## 收尾、证据与限制

临时Steam启动选项已恢复为空，专用测试服务已停止，游戏测试进程不存在。14929个导入文件的大小/SHA256再次核对通过，描述符名称未变，默认userdir的内容和mtime未变。VM及本地桌面保留。

本机证据根目录：D:/Astra-Paradox/artifacts/linux-native-arch-2026-10-14/native-implementation。最新记录为linux-cb15e8c-validation.json、nickname-scene-validation.json、input-cb15e8c-validation.json、arch-loader-cb15e8c.json、cleanup-and-assets-cb15e8c.json、build-cb15e8c-manifest.json与evidence-index.json。此前输入记录packaged-scene-validation.json、Windows记录windows-c0e0940-validation.json和失败日志单独保留。日志、描述符、启用列表、树快照、截图及CI包可复查；游戏程序、汉化载荷、密钥、Steam账户状态与core不提交仓库。

仅验证指定ELF完整哈希和当前Arch ABI，测试使用X11及Mesa llvmpipe软件渲染。IME预编辑/候选定位、Linux拼音搜索控件、动态字体/复杂排版、姓名顺序和Steam状态扩展仍未接入。选择/鼠标命中、二次扫描/独立直接安装、Wayland、其他发行版、硬件GPU、完整战役/存档往返以及Steam在线联机/云存档/Overlay界面交互未验证。客户端阻止非回环IP访问；原生启动、API初始化与Overlay库共存已验证。

## 2026-10-16 正文换行回归

上述限制对应早期基线阶段；后续四组功能的当前实现范围见 [Linux 原生适配](linux.md)。本轮针对中文正文越过右边框及单字成列的问题，增加独立 Linux 排版模块，保留已有 bitmap 字库与原生绘制。

代码提交 `23c270696acf570702f6de0e6245043c4bc90004`，CI `38768058375` 的 Windows 26 项、Arch 14 项检查全部通过。原生测试 `layout-native-23c2706` 使用独立 runtime、显式 userdir、已有主汉化和补充包本地副本，游戏进程 PID 8248。SO SHA256 为 `ff6d99d867824f9738f3aa482aac9fd69a0ee415b283b9751434336507917f4f`，游戏完整哈希仍与原生接口表一致。

| 原生场景 | 观察结果 |
| --- | --- |
| 奥斯曼崛起选国说明 | 全段在边框内换行，“白骑士”连续显示 |
| 东方王朝选国说明 | 长段落在原内容区内换行 |
| 明朝开局介绍 | 五段正文和彩色词语完整，末段在底部国家图标上方结束 |
| 右侧提示窗口 | 长说明按窗口宽度换行 |
| 大明时局事件 | 文言正文在纸张范围内连续排布，按钮边界正常 |

游戏正常退出 0，默认用户目录全部文件的内容哈希与 mtime 不变。本地化导入 253544 条、拒绝 0 条；脚本 BOM 消耗 5756 次。退出日志记录屏幕、纹理及测量边界处理次数分别为 16095、478、224778，这些数值是断行机会处理次数，不能当作最终换行数。

证据位于工作目录的 `artifacts/linux-four-features-2026-10-15/TEXT-LAYOUT-VALIDATION.md`、同目录 `text-layout-before-*.png` / `text-layout-after-*.png`，以及 `artifacts/linux-native-arch-2026-10-14/native-implementation/tests/layout-native-23c2706`。保留修复前后的日志、测试描述符、启用列表、可执行文件与 SO 哈希。临时 Steam 启动选项已恢复为空，专用离线测试服务已停止。

本轮使用 softpipe。另行完成的无补丁原版对照确认：VM 的 llvmpipe 路径也会出现逐省杂色，softpipe 对照恢复一致国色；本轮段落修改没有根治该驱动问题。硬件 GPU、Wayland、其他发行版及其他游戏版本不属于本轮验证范围，动态字体/GPU 复杂段落集成仍未完成。

## 动态缺字与强制换行补测

功能提交 `6942ee0` 加入独立 Linux 字体分页与 GPU 批次模块。CI `38780257678` 通过 Windows26/Arch14，SO SHA256 `7b2e49cd04c387903823f8318e49ed79e343a05e9ef415f94b3031a8a4fef002`。真实隔离运行 `fonts-native-6942ee0`、PID9807 显示了原界面字库缺少的 Ω、Ж、𠮷，彩色文本正常，但混排长段落在强制换行前仍出现句尾越框。该失败画面、完整日志和退出0记录保留，默认 userdir 内容和 mtime 未变。

`a5492ec` 修正原生 CR/LF 分支跳过最后待测词段的问题，所有 Linux 绘制/测量入口使用相同的词段提交判断。最后功能树 `1fe273b8d74c6c7bb0f83adffb267740d35933d9` 的 CI `38784338280` 通过 Windows26/Arch14，SO SHA256 `d3c691a245172c995a75684f008c2857c2a8902ecd6559fe8cf36a7c808713e9`。CI `38783396300` 的测试预期失败也保留；修正后不再错误禁止未格式化 section sign 前的普通 ICU 边界。

最终原生测试 `fonts-native-2912636`、PID10086 的标签在前一 CI 时分配，实际部署的是上述成功 CI 的 1fe273b SO，按完整哈希核对。显式独立 userdir、新增 probe mod、原主/补充包本地素材、softpipe 和私有 Mesa cache。实际通过：Ω/Ж/𠮷 混排、颜色和空行、强制换行前句尾不越框、东方王朝说明、明朝五段介绍、开局事件及右侧提示。最终日志记录6个动态字形、4次页上传、497个原生 GPU 字体绘制批次，未出现字体上传/捕获/绘制失败。本地化253548条，拒绝0条。

国家侧栏、顶部名称、介绍标题和提示框已显示 probe 的“明𠮷”；地图字库为缺失的𠮷生成字号88的页并上传。完整曲线地图标签的无遮挡截图尚未取得，这一项保留为未验证，不能仅凭上传计数宣布曲线几何通过。普通中文国名和省名仍可见。softpipe 国色一致，111个明朝省份的 CPU 颜色缓冲均为 `6880b3ff`。

采集器在1800秒时收集了一次游戏仍运行的记录，随后另存 `post-exit-evidence.tar.gz`、最终日志及 `post-harness-exit.json`，不覆盖原始采集结果。游戏正常退出0，默认 userdir 文件内容和 mtime 未变；临时 Steam 选项恢复为空，专用离线服务已停止。完整证据见 `artifacts/linux-four-features-2026-10-15/NATIVE-FONT-PAGES-VALIDATION.md`、`dynamic-font-hardbreak-comparison.png`、`dynamic-font-hardbreak-result.json` 和 `artifacts/linux-native-arch-2026-10-14/native-implementation/tests/fonts-native-2912636`。

独立标量缺字分页已进入真实界面，完整复杂段落 shaping/bidi 仍未接入；Latin-1 缺字的部分原生直接查表、曲线缺字完整画面、生命周期压力及硬件 GPU 仍待补齐。四组功能的发布目标保持未完成。

本次没有更新工坊、改订阅/日常播放集/存档或自动发布Release。草稿PR9保留待审阅。

## Latin-1、地图索引及提示框行拆分补测

`5b7a24a` 修复原生格式解析后单字节缺字直接查表的路径。独立字库副本移除 é，Å 在原字库中已缺失；修复前两者消失，修复后白色与黄色正文均完整显示，Ω、Ж、𠮷 及换行保持正常。原汉化资源未修改。

只读几何快照进一步确认：地图 CurveText 临时生成六顶点，实际上传压缩为每字形四角点，使用原生六索引。`92fbc62` 修复分页对该格式的处理，保留原索引缓冲及 winding。`5a84f3b` 把 GPU 页上传限定到真实绘制线程；`6ba1598` 在上传前选定 unit0，并恢复原 active texture unit，保护地图 fog 所在的其他纹理单元。

真实提示框还暴露 `CTextBuffer::GetLineCharSize` 按字节二分行长的问题：它返回汉字内部的字节位置，后续原生行串从续字节开始，触发 U+00E7、U+00E4、U+00B8、U+00AA 的错误缺字请求。`2ac7be0` 在该原生入口按共享完整格式字素前缀测量，并优先选择 ICU 断行机会；原控件宽度、padding、字体字宽及行对象所有权保留。170 处 profile 指令均与完整目标 ELF 重新核对一致。

CI **38809455419** 对功能树 `2ac7be0f7bfeb84b3a574d61887565b7ac193d88` 的 Windows26/Arch14 检查全部通过。原生隔离运行 `text-row-native-2ac7be0`、PID12753，SO SHA256 为 `0c739d4ac389fb546ffee3b6ccc0eb57708558ed1ef61ad0db6d4b432fa2a778`，与实际映射文件核对一致。

| 原生场景 | 结果 |
| --- | --- |
| 缺字混排、黄色文字及长段落 | é、Å、Ω、Ж、𠮷 完整显示并留在原框内 |
| 法国盾牌提示 | 跨行汉字完整，无续字节变成额外字符，行框内排布 |
| 东方王朝说明 | 长段落在内容区内换行 |
| 单个 U+20BB7 地图标签 | 完整笔画，保留原生曲线方向；已有国名仍正常 |
| 政治地图 | softpipe 国色一致；不把该结果解释为修复 llvmpipe 驱动缺陷 |

测试描述符、启用列表、本地化和字库哈希与修复前的 `font-trace-cfb11fe` 一致。最终记录：7 个动态字形、3 次上传、492 个字体批次，其中152 个为原生索引地图批次；新行长入口执行3 次。无上述错误碎片请求，也没有字体捕获、上传、绘制、绑定、释放或线程边界失败日志。本地化253546 条、拒绝0 条，脚本 BOM5756 次。运行1503.44 秒，正常退出0；默认 userdir 的内容哈希和 mtime 未变。证据为本机 `LATIN1-AND-MAP-QUAD-VALIDATION.md`、`text-row-native-result.json`、`text-row-native-comparison.png`、`text-row-map-gpu-after.png` 及对应原生测试目录。

同一轮查找窗口收到完整 `beijing`，证明 softpipe 的输入延迟不是事件丢失。它能找到北京，但精确模式还错误收进容错候选贝宁。该独立问题由后续搜索分层修正处理；不能把这次发现记录成完整搜索矩阵通过。复杂段落 shaping/bidi、IME 下划线及选区、完整搜索矩阵、真实生成姓名及 Steam 载荷、生命周期压力仍未完成，发布目标保持进行中。

## 搜索分层与 IME 候选位置补测

`a42f16d` 修复原生查找窗口的分层语义：两个 Find 调用点只接收直接 Unicode/普通拼音匹配，容错拼音进入后续原生距离分支，外交的独立过滤策略保持原样。CI38849204167 通过 Windows26/Arch14。真实运行 `search-ime-native-a42f16d`、PID13133，SO SHA256 `914f9825fef80e2c9dc3d7dcd65e08125b631f063a8a7db03deb18b0e739b0bd` 验证了：勾选精确时 beijing 仅保留北京；不勾选时北京排在容错贝宁之前；flx 找到法兰西国家；Fcitx 提交的北京也正确查询。bj 的多个首字母候选已有画面，但该图不证明北京在列表中的具体位置。运行1703.46 秒，正常退出0，默认 userdir 内容和 mtime 未变。

同一轮首次证明行内预编辑进入真实画面：未提交 bei jing 时原生文档为空，Space 后得到北京、字节列6。候选条却偏到弹窗左上方。`babd836` 使用原生光标精灵屏幕位置消除父级变换偏移；其首轮画面仍覆盖输入行，失败记录保留。后续反汇编确认 CTextBuffer 虚表1d0 返回缓存光标位置，不能测量 M 或预编辑前缀。

`30a02fc` 改用原生字体的字符串宽度和行高。CI **38854694127** 通过 Windows26/Arch14。真实运行 `ime-metrics-native-30a02fc`、PID13833，映射 SO SHA256 `a4c0d046206183b52788eb61051f9feb50d56a0a75f85c796d092fabdf8d8b86`，验证候选条位于输入框下方、预编辑可见且文档为空；Escape 清理预编辑但保持输入框焦点和空文档；重新组合并提交后得到两个完整汉字。运行1667.29 秒，正常退出0，默认 userdir 内容和 mtime 未变。172 处 profile 指令均与目标 ELF 完整核对一致。

此轮使用真实 X11/Fcitx 键盘输入及只读原生缓冲快照，没有注入事件或调用运行中的游戏对象。候选位置视觉通过限定在已测查找弹窗、GUI scale1.0。XIM 反选反馈可能令 SDL start/length 表示高亮范围，不能将 start0 或一次 Left 当作内部插入光标正确的证明；预编辑下划线、选区及更多控件/缩放仍待完成。

证据为 `artifacts/linux-four-features-2026-10-15/SEARCH-AND-IME-VALIDATION.md`、`search-ime-native-result.json`、`ime-font-metrics-native-result.json`、原生反汇编、查询快照及对应截图。当前测试结束后已恢复原来的空 Steam LaunchOptions，保留其他当前配置字节，并停止专用离线 Steam 服务，见 `text-row-search-ime-cleanup.json`。完整繁体/混合/英文/外交搜索矩阵、复杂 shaping/bidi、姓名与 Steam 载荷及生命周期压力仍未完成；Linux 发布目标保持进行中。

## 预编辑视口、装饰与中文鼠标命中补测

`62dca27` 接入完整格式字素视口和独立 IME 装饰绘制模块，保留长串的活动尾段；`117fa18` 用实际文字内容起点修正下划线/候选偏移，并清理空预编辑事件。真实多人输入框还复现光标落在汉字内部 byte13 的问题，`415bcdc` 改为完整格式字素命中，`56e6e28` 再扣除控件 border/font padding。

`9163edf699ed2aec94f2faebbd8759b3e53f783e` 修复 1.25 倍缩放下屏幕鼠标坐标与未缩放精灵位置混用的问题，限定 Unicode 非密码输入框，在原生减法前调用游戏自己的整数坐标转换。CI **38887257113** Windows26/Arch14 全部通过；SO SHA256 为 `e56192c2108b0a11ee0e1f74ee4a16780a89c594822837ed626e84f4f9559684`。当前185处 profile 签名均与完整目标 ELF 重新核对一致。

| 原生运行 / 实际 SO | 控件与缩放 | 实测结果 |
| --- | --- | --- |
| ime-ranges-native-117fa18 / e90a0d6… | 原生 Instant 多人过滤框，font14，1.0倍 | 短串装饰、取消、54字节长串尾段、2包12汉字提交、失焦清理通过；同时保留 byte13 鼠标缺陷的修复前证据 |
| editor-hit-native-415bcdc / d87d50c… | 同一原生 Instant 控件，1.0倍 | 相同点击改为完整 byte15，插入结果与捕获字节位置一致；仍有内容起点偏移，后续修正 |
| ime-texture-native-56e6e28 / f2d9d3d… | 私有 GUI 探针的 CTextSprite 查找框，font16，1.0倍 | 实际虚表方法0x2097208；短串、取消、长串、中文提交、byte9中间插入通过 |
| editor-origin-native-56e6e28 / f2d9d3d… | 原生 Instant 多人过滤框，1.0倍 | 修复前相同点击[811,205]改为正确 byte9，长串及中间插入通过 |
| ime-scale125-native-9163edf / e56192c… | 同一 Texture 探针，1920×1080，实际1.25倍 | 相同点击[249,205]由修复前 byte24 改为正确 byte9，缩放装饰、长串、中文提交及中间插入通过 |
| editor-unit-native-9163edf / e56192c… | 原生 Instant 多人过滤框，1280×720，1.0倍 | 同一最新二进制默认缩放回归通过，点击 byte9，提交后光标15 |

各项验收来自真实 Fcitx/XIM 键盘、游戏原生文档与 sprite 的只读快照、日志和画面。长串为54字节拼音，提交准确得到“北京北京北京北京北京上海”（12标量/36字节、2个SDL包）；最新两个版本在 byte9 预编辑“ce shi”期间原文档不变，Space 后准确得到“北京北测试京北京北京北京上海”（14标量/42字节、光标15）。取消、提交及失焦时装饰与预览快照清空。

每个独立装饰验收进程的最初32次真实绘制均核对 GL program、矩阵、颜色、裁剪、blend 与 legacy texture unit，零不一致；Texture 与 Instant 各自使用新的进程取得这32次记录。最新两轮分别为 PID19508/19838，运行912.90/1125.73秒，正常退出0，默认 userdir 内容哈希和 mtime 不变。本地化导入253544条、拒绝0条，脚本 BOM5756次。已有正文、图标和地图字体继续使用原汉化资源。

失败记录同样保留：第一版装饰因光标图片透明边距偏高；1280×720的1.25倍请求被游戏重置为1.0，不能当作缩放通过；1920×1080下 `56e6e28` 实际复现了 byte24 的缩放命中偏移，`9163edf` 在相同夹具上修正。两种缩放下选国说明的完整段落均在原边框内。

证据为 `artifacts/linux-four-features-2026-10-15/IME-PRESENTATION-AND-MOUSE-VALIDATION.md`、`ime-latest-native-results.json`、`ime-and-hit-builds.json`、`editor-scale-profile-verification.json`、对应截图，以及 `artifacts/linux-native-arch-2026-10-14/native-implementation/tests/` 下各同名运行。临时 Steam LaunchOptions 已恢复为空，保留其他当前VDF字节，专用离线服务已停止；VM显示分辨率已从测试用1920×1200恢复原1280×800。没有残留的隔离游戏进程。

1.25倍验收限定该 Texture 查找框，1.0倍覆盖两种绘制分支。普通文档的真实选区替换、其他缩放/密码框、预编辑期间鼠标行为仍需验证。复杂段落 shaping/bidi、完整搜索矩阵、真实生成姓名及Steam载荷、生命周期/安装压力和最终Windows/Linux回归仍未完成。上述IME回归使用llvmpipe，不代表修复了其原版也会出现的政治地图杂色；早期softpipe的颜色对照保持独立。草稿PR9未合并，Linux发布目标保持进行中。

## 原生文档选区及复杂 UI 段落

2026-10-16 继续验收原生 Instant 多人过滤框，font14、GUI scale1.0。`selection-native-6ef3c02`、PID21017、SO仍为 e56192c…，原生 Shift+Home 选中 `abcde` 后，预编辑投影为 `ce shif`，真实文档/选择仍是 `abcdef` / `abcde`；取消保留两者，提交得到 `测试f`。原生 Shift+End 全选 `测试f` 后，`bei jing` 预览和取消均不改变原文/选择，提交准确得到 `北京`，光标字节列6、选区空。32次装饰 GL 状态检查零不一致。完整函数调查确认该原生控件没有 Shift+Left 选区分支；失败初试和拖动不建立选择的记录均保留，不据此增加额外游戏快捷键。

该观察器曾在期限结束时保留“游戏仍运行”的中间结果，最终正常退出0、默认 userdir 未变的记录另存，不覆盖历史结果。证据为 `NATIVE-SELECTION-VALIDATION.md`、`native-selection-result.json`、各原生快照及 `post-harness-exit.json`。这项结果只覆盖该真实控件，其他编辑器、密码框及组合期间鼠标仍需验收。

复杂段落功能树 **aa917ddc34be74c9404e89db749ce44383f92987** 的 CI **38910831302** 通过 Windows27/Arch16。共享 `paragraph_transport` 的两平台测试使用真实 shaping/rasterization 核对像素重建、内联命令、强制换行、缩放整数进位、PUA隔离和长单词暂存分段；Linux 新增实际 ParagraphScope 的嵌套、字库别名、传输副本和所有权测试。原生实测 SO SHA256 为 `ee9edf3780e5a6667fa174c752c217257473379e246daa9a79b41f2f7cd43277`，187处 profile 签名全部重新核实。

| 原生运行 / PID | 结果与实际边界 |
| --- | --- |
| paragraph-before-ba4e3d4 / 26490 | 旧 e56192c…包的相同样例显示独立阿拉伯字形、逻辑序希伯来文和未连写印地文；正常退出0 |
| paragraph-native-ba4e3d4 / 26750 | 首版新包正确拒绝超过原单词暂存容量的传输串，packed0；保留失败日志、截图和正常退出0记录，不算 shaping 通过 |
| paragraph-native-aa917dd / 27100 | 原生 screen 路径成功打包/上传1份段落，6行规划、55 tokens、619bytes；阿拉伯文连写、Hebrew/Hindi/组合字符与原中文、颜色、图标混排在说明框内。普通东方说明与返回也正常；退出0，484.21秒 |
| paragraph-texture-aa917dd / 27335 | 错误夹具把游戏固定要求的 scenario_description InstantTextBox 改成另一类型，原生界面查找失败并崩溃；退出11，报告取消，记录保留 |
| paragraph-static-texture-aa917dd / 27568 | 改用正常 GUI 工厂创建的独立 TextBox 子控件，原生 texture 与 screen 都成功绘制同一段文字；唯一键第二段复杂说明与第一段切换/返回正确。几何及上传数1/1→2/2→3/3，返回保持3/3，无独立标量缺字回退请求；正常退出0，429.79秒 |

五轮均保留原生启动参数、描述符、启用列表、日志、完整可执行文件/SO哈希与默认 userdir 文件内容/mtime 未变的结果。主样例文件SHA256保持 `29261513b40a73c1575f4340301151a7d9e8af1c016dd5af6ad3881d6e871e4e`。前一个第二段本地化覆盖没有胜出，保留为无效夹具；成功版本使用自有 bookmark 资产指向唯一键。没有修改原模组或游戏界面资源。

证据为本机 `PARAGRAPH-NATIVE-VALIDATION.md`、`native-paragraph-result.json`、`paragraph-native-*.png`、`paragraph-native-verified.txt` 和各同名原生运行目录。CI中测试字面量类型错误、缺少声明头文件、Pango相邻对象被合并丢失的失败均保留；后者由真实长对象回归检出并修正。屏幕及纹理日志中的行数、宽高是规划指标，不能充当原生绘制后行数的精确测量。完整裁切/尺寸一致性、末尾空白换行、更多缩放、复杂编辑几何和地图cluster绘制仍是开放验收项。

最后成功测试已正常关闭，临时 Steam LaunchOptions 恢复为空，保留其他当前VDF字节，并只停止专用离线服务；见 `paragraph-native-final-cleanup.json`。没有残留自有游戏或观察器。Windows新DLL仅通过本轮27项CI测试，旧版原生游戏矩阵不归到新DLL；最终两平台游戏回归、搜索/姓名/Steam载荷、生命周期/重扫/安装压力、硬件GPU/Wayland等仍未完成。PR9保持草稿，本轮未合并或发布。

## 原生编辑器的 UTF-8 写入、容量与视觉光标

2026-10-17 的功能树 `b1b74658d801d9dd2dbe3793d1d7733f8b375f64`，CI **38952449164** 通过 **Windows27 / Arch18**。SO SHA256 为 `36b71879c4b68b630026f5c849dc11f80b1a8c8854c008dc09ececb81f36011e`；完整目标 ELF不变，全部219处 profile 字节及新增挂钩的真实指令边界重新核实。

原生剪贴板直接返回 UTF-8，Write 的逐字节禁用表却会删除 Arabic Alef 中的A7以及其他组合/印地文字续字节。独立 mutation 模块改为完整标量过滤，保留实际禁用符号的限制；追加、行首和中间路径的五处 substring 对齐完整字素/格式边界，仍以原字节容量为上限。行首直接存入输入字节数的分支同时限制光标到实际文档末尾。原生暂存 CString 的构造、赋值、析构及通知不变。

复杂编辑器用完整行 ShapedParagraph 处理命中、视觉移动及光标缓存。修复前相同17点扫描的光标像素为 `0,0,9,39,39,20,28,28,39,43,43,51,51,51,51,51,51`，原生命中记录已经是正确 byte2/trailing，随后却被焦点清理丢弃。只改共享记录仍复现，因此归因到焦点清理而不是线程切换。文档 affinity 改为按编辑器身份及自有文档/行列保存，失焦保留，销毁删除；IME 预编辑仍独立清理。最终相同扫描为 `0,0,9,13,13,20,28,28,39,43,43,51,51,51,51,51,51`，同时保留 RTL 段的逻辑反向。

| 原生运行 / PID | 结果 |
| --- | --- |
| editor-physical-1c95943 / 1726 | 保留 Arabic/组合粘贴坏字节；希伯来文及中文移动通过。观察期限到达的历史结果保留，最终 exit0 另存。 |
| editor-mutation-21eff77 / 5306 | 普通190px控件的 Arabic/组合/中文移动与禁用字符通过；鼠标扫描失败，记录保留。exit0，默认 userdir不变。 |
| editor-capacity-before-1c95943 / 9411 | 隔离加宽控件三种容量路径均切断 UTF-8；行首光标253越过250字节文档。exit0，默认 userdir不变。 |
| editor-final-153845c / 10195 | 四种容量用例通过，鼠标扫描仍失败。exit0，默认 userdir不变。 |
| editor-verified-b1b7465 / 12149 | 同一最终产物通过7种 mutation、3种移动及17点鼠标扫描。 |

最终 mutation 为 Arabic、组合字符、禁用字符、追加容量、行首容量、中间容量和组合边界容量。后四种实际文档/光标分别为249、249、247和248字节，均有效且满足250字节上限；旧版行首曾留下坏UTF-8和光标253。容量对照仅在独立模组中把真实原生 playername 宽度设为4096，以分离像素溢出与字节容量，修复前后夹具 GUI SHA256相同；原界面资源未修改。其他字体、事件、通知及资源加载均为真实原生路径。

这些运行是专用 systemd unit 启动的原生游戏，连接已运行的 Steam 客户端，保留成功 SteamAPI日志和 cgroup，不称为客户端派生运行。详细证据在本机 `NATIVE-EDITOR-VALIDATION.md`、`native-editor-verified.json`、`editor-native-verified.txt`、`editor-paste-native-verified.txt` 和各同名原生目录。默认 userdir 内容及 mtime、原生正常退出、Steam临时选项恢复及专用服务停止由独立审计脚本核实。

验收范围为该 Instant 控件与GUI scale1.0；新 Windows DLL仅通过CI。复杂双向选区的多区域绘制、复杂预编辑视口与组合期间鼠标、地图cluster、更多控件/缩放/密码框、完整搜索矩阵、真实生成姓名/Steam载荷、生命周期/安装压力与最终两平台游戏矩阵仍待完成。PR9保持草稿，Linux发布目标继续进行。
