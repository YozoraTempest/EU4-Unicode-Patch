# Arch 原生游戏验证记录

记录日期：2026-10-15（Asia/Shanghai）。目标是 Steam EU4 1.37.5.0 Inca Linux x86_64原生游戏，基准环境为Arch Linux；没有使用Proton。Windows/Linux共用八个Unicode核心库、版本及项目，分别构建DLL/SO。

## 最新源码、CI及哈希

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
