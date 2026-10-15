# Arch 原生游戏验证记录

记录日期：2026-10-15（Asia/Shanghai）。适配目标为 Steam EU4 1.37.5.0 Inca Linux x86_64 原生游戏，基准环境为 Arch Linux；没有使用 Proton。Windows 与 Linux 共用八个 Unicode 核心库、同一版本与项目，分别构建 DLL 和 SO。

## 源码、CI 与二进制

原生模块源码提交为 `c0e09400c9ad724b92f3558fbb36296a2656c802`；最终启动器修正提交为 `55fa3a1f17451672f501f31441b89b9e8927994c`。后者只修改启动器、入口测试和 Linux 文档，C++ 与 Windows 源码没有变化。

最终 CI **38666305982** 成功：Windows 26 项 CTest、Arch 10 项 CTest，零失败。CI 合并提交为 `c7fbb7aa7d51a01cf26fd0e64e20104fe91f146a`。此前原生模块 CI **38658319276** 也全部通过；九个核心程序在完整 Arch VM 中重新运行成功。最终加载器测试又在完整 VM 中通过，包括辅助程序转发、旧 OpenSSL 符号陷阱、完整文件 SHA256、未知 ELF 拒绝、带空格的游戏目录及 C/POSIX 字符区域。

| 文件或产物 | SHA256 |
| --- | --- |
| Linux 游戏 ELF，Steam build 15918133 | `af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3` |
| 最终 Linux libeu4_unicode.so | `23f626248df6a269996f765edd5411f4638ec63e0a6b7cc577f50923e95388ed` |
| 最终 launch-linux.sh | `d7e558ebcad2e0882f4111e16ec27ef3e3a292c6af944a2cd4613e7b22e49a9d` |
| Windows 游戏 eu4.exe | `9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a` |
| 完成 14 组游戏回归的 Windows DLL，CI38658319276 | `1775c8077fdabb21a4001f9c07c0191000165ebccaeda6aa738a9a17e61a3d67` |
| 最新 CI38666305982 Windows DLL，CI26项通过 | `efa1a7dc937b121e30bfab9de51a8dcb8d2cfc0f31f580f9de677f4effb1d5bd` |

最新与已测 Linux SO 完全相同，可直接对应下列 16 组回归。Windows 的 14 组实测记录对应表中明确标注的前一批 DLL；最后一轮只改 Linux 启动器，没有重复运行相同 Windows 源码的游戏矩阵。所有 C++ 构建均由 CI 完成。

Arch VM 使用 X11、Fcitx5 拼音、sdl2-compat 2.32.74、SDL3 3.4.18、ICU 和 Mesa llvmpipe 软件渲染，6 GiB RAM / 4 vCPU。每次启动使用隔离 runtime 和显式 `-userdir`。VM 中没有下载创意工坊内容。

## 原生登记和资源加载

最终矩阵使用一份 CI SO，16/16 组原始结果全部通过。每组在进入菜单后保留 30 秒，核对实际模块映射、只读原生登记树、原始名称、描述符路径、dependencies、setup.log 及字体日志。另有五组无插件真实游戏对照；未启用同名登记能复现英文，零个或多个启用候选的原生选择得到保留。

| 用例 | 场景 | 原生本地化结果 | 结果 |
| --- | --- | --- | --- |
| vanilla | 无模组基准 | Rival / Hostile relations | 通过 |
| unique | 正常汉化，无同名登记 | 宿敌 / 敌对关系 | 通过 |
| old-disabled-first | 旧工坊形态未启用候选先扫描 | 宿敌 / 敌对关系 | 通过 |
| old-enabled-first | 已启用候选先于旧工坊形态候选 | 宿敌 / 敌对关系 | 通过 |
| local-disabled-first | 未启用本地副本先扫描 | 宿敌 / 敌对关系 | 通过 |
| local-enabled-first | 已启用候选先于本地副本 | 宿敌 / 敌对关系 | 通过 |
| none-enabled | 同名候选全部未启用 | Rival / Hostile relations | 通过 |
| multiple-enabled | 多个同名候选同时启用 | OLD_NATIVE_FIRST / Hostile relations | 通过 |
| multiple-enabled-inactive-first | 未启用候选先扫描，另两份同名候选启用 | Rival / Hostile relations | 通过 |
| dependency-forward | 旧名称依赖，正常启用列表 | 宿敌（依赖覆盖验证） / 敌对关系 | 通过 |
| dependency-reversed | 旧名称依赖，反转启用列表 | 宿敌（依赖覆盖验证） / 敌对关系 | 通过 |
| dependency-control | 移除探针依赖的因果对照 | 宿敌 / 敌对关系 | 通过 |
| path-backslash | 原生不接受的反斜杠启用路径 | Rival / Hostile relations | 通过 |
| path-absolute | 原生不接受的绝对启用路径 | Rival / Hostile relations | 通过 |
| archive-disabled-first | 归档汉化，未启用同名目录先扫描 | 宿敌 / 敌对关系 | 通过 |
| archive-enabled-first | 归档汉化先于未启用同名目录扫描 | 宿敌 / 敌对关系 | 通过 |


依赖三组均仅启用主汉化与 ZZZ 探针；补充包描述符被扫描但未启用。探针覆盖同一个 `diplomacy_l_english.yml`。声明旧名称依赖时，正反启用列表都显示“宿敌（依赖覆盖验证）”；移除依赖则显示“宿敌”。原生树键与对象 name 始终是 `Chinese Language Mod for 1.37`，没有别名或 ID 特判。

工坊形态冲突候选使用真实描述符字段与小型自有标记载荷。UTF-8 主汉化、补充包、地图字库及 ZIP 来自本机已有实际素材。Linux 影子候选不等同于完整旧工坊载荷；Windows 的 14 组回归使用完整旧资源。早期测试夹具的英语大小写、扫描顺序和依赖干扰问题及原始失败记录均保留，未拿它们替换最终 16 组结果。

## Steam 启动、画面与输入

实际 Steam 启动曾发现两个问题，并已修复：游戏导出的旧 OpenSSL 符号与系统 OpenSSL3 上下文混用，引起校验前崩溃；Steam 的 `LC_ALL=C` 又令 XIM 可以显示候选却不能提交中文。前者改用固定版本 PicoSHA2 并限制 ELF 导出，后者由启动器将 C/POSIX 转为 C.UTF-8。C 数字格式保持不变。

受控 UTF-8 区域测试 `plugin-c0e0940-steam-utf8locale`（PID11295）经实际 Steam 客户端启动，SteamAPI_Init 加载 steamclient 成功，64 位 Overlay 库同时映射。中文菜单、地图标签及长段落可见，原始 49,049,728 字节地图 DDS 加载为 6400×7664；导入253544条本地化，拒绝0条。真实 Fcitx 提交后只读原生缓冲区为 `player测试`，字节 `706c61796572e6b58be8af95`，光标列12；左移到列9，Delete和Backspace后为 `player`、列6。该进程运行 811.31 秒，正常退出0，默认 userdir 未变。

最终 CI tar 包 `plugin-55fa3a1-steam-final`（PID11619）未使用测试性的区域覆盖；由包内启动器自动得到 C.UTF-8，同样在 Overlay 库加载时完成上述中文提交、左移和删除，实际映射来自最终包目录。随后外部会话/VM中断，未取得该轮地图截图与正常退出码。记录明确保留中断，不宣称该轮正常退出；地图和退出证据引用前一轮完全相同 SHA256 的 SO。两轮来源分别保存在验证 JSON 中。

测试后临时 Steam LaunchOptions 已恢复为空，专用临时 Steam 服务因 VM 重启已结束，游戏与 Steam 测试进程均不存在。全部14929个导入资源再次核对大小与SHA256通过；名称未变，默认 userdir 的内容及修改时间未变。VM桌面保留。

## 证据与功能限制

本机根目录为 `D:/Astra-Paradox/artifacts/linux-native-arch-2026-10-14/native-implementation`。`linux-validation.json`、`windows-c0e0940-validation.json`、`packaged-scene-validation.json`、`steam-scene-c0e0940-validation.json`、`arch-loader-55fa3a1.json`、`cleanup-and-assets.json` 和 `evidence-index.json` 记录逐组证据、哈希及来源。原始描述符、启用列表、日志、树快照、截图和 CI 包均保留。游戏程序、汉化载荷、密钥、Steam账户数据与崩溃core不提交仓库。

仅验证上述 EU4 ELF 完整哈希和当前 Arch 依赖 ABI。IME预编辑及候选窗定位、Linux拼音搜索控件、动态字体与复杂排版、姓名顺序和Steam状态扩展尚未接入；Steam昵称自动填入仍可见乱码。选择与鼠标命中、运行中二次扫描、独立直接安装路径、Wayland、其他发行版、真实GPU、完整战役与存档往返尚未验证。多份同名模组同时启用保持原生处理。

Steam客户端测试期间阻止非回环IP访问。验证了原生启动、SteamAPI初始化和Overlay库共存；没有验证在线联机、云存档或Overlay界面交互。没有发布Release、更新工坊、改订阅、改日常播放集或修改存档。源码保留在草稿PR9等待审阅。
