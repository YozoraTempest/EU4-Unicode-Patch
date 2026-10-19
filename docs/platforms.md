# Windows 与 Linux 共用结构

两个平台使用同一项目、同一个 `VERSION` 和同一套 Unicode 核心源码，分别构建原生二进制。Windows 玩家补丁使用 DLL；Linux 原生游戏需要 ELF 共享库 `.so`。Windows DLL 不能作为 Linux 原生游戏插件直接加载。

## 构建边界

顶层 `CMakeLists.txt` 根据目标系统选择平台构建入口：

- `cmake/shared-core.cmake` 集中定义九个共享库、依赖和拼音数据生成。
- `cmake/windows.cmake` 配置 Windows ICU 导入、MASM、原生适配、DLL、现有工具与测试。
- `cmake/linux-core.cmake` 配置 ICU 头文件、运行时 ICU 解析、PIC、Linux 核心测试及 ELF 游戏插件。

Windows 的 `native_editor_text.cpp` 和 `native_search.cpp` 通过 `target_sources` 加入现有目标；共享构建定义不包含这两个原生适配实现。现有 CMake 目标名称及使用这些实现的 Windows 消费者保持原有链接契约。

## 共用范围

| 内容 | 共用方式 |
| --- | --- |
| UTF-8、字素与行边界、Unicode 服务 | 同一源码，由各平台提供 ICU 链接目标 |
| 格式化文本、编辑模型、拼音搜索 | 同一算法、同一拼音数据生成规则与核心测试 |
| 字形登记、字体路径规则、绘制批次 | 同一数据及算法源码 |
| 版本管理 | 共用根目录 `VERSION` |
| 汉化、本地化及字体素材 | 在隔离 Linux 副本中使用原资源，描述符路径适配到本地环境 |
| 原生引擎接口 | 分别调查 Windows PE 和 Linux ELF，分别验证参数、布局和生命周期 |

现有 `native_text_event` 测试仍携带已调查的 Windows 输入事件契约。算法能够在 Linux 上编译和通过测试，不代表 Linux 游戏的输入结构与该契约相同。

## 必须由平台适配的部分

| 接口 | Windows 现有实现 | Linux 原生目标 |
| --- | --- | --- |
| 加载入口与指令挂钩 | version API 代理、MinHook、MASM | LD_PRELOAD、SafetyHook、main 之前安装 |
| 可执行文件校验 | PE 映像、代码及内存权限检查 | ELF ABI、主程序映射、每个原生位置的指令；SHA256 仅作诊断 |
| 字体布局与栅格化 | DirectWrite | 原模组 BMFont 优先，Pango/Cairo/Fontconfig 提供动态缺字与复杂段落 |
| 图集、纹理与绘制 | D3D9 与 Windows 游戏调用 | Linux 原生 UI、缓存纹理、顶点及地图字形调用 |
| 输入法与控件接入 | IMM、Windows 消息及 SDL 事件入口 | SDL 提交与预编辑、原生 CEditBox、字素编辑及所测控件的鼠标命中、复杂编辑几何及 Ctrl+A 全选 |
| 模组登记 | Windows 原生容器、字符串和登记上下文 | 独立 Linux 扫描上下文及原生描述符赋值 |

共享 Unicode 语义，平台层负责转换原生参数和资源。指令地址、引擎字符串、容器成员偏移与析构规则不跨平台复用；不得用现有 Windows 数据样例代替 Linux ABI 验证。

## 当前支持状态

Windows 构建生成 DLL，Linux 构建生成 `libeu4_unicode.so`。Linux CI 在 Arch、Ubuntu 22.04 和 Ubuntu 24.04 验证九个共享库、拼音模糊搜索、Pango 排版、段落传输与真实预加载入口的转发及未知 ELF 拒绝行为；原生游戏实测另外进行。

Linux 接口依据来自已安装的 Steam EU4 1.37.5 原生程序，详见 [Linux 接口表](linux-native-profile.md)。插件不再在 ELF 依赖中固定 ICU 主版本；Ubuntu LTS 和 Arch 共享同一套 ICU C API 运行时解析，Steam Linux Runtime 和其他发行版仍需实际验证。

同一次 CI 运行同时验证 Windows、Arch 和 Ubuntu 22.04/24.04，并保存各平台产物。Linux 包仍只针对 x86_64 原生 EU4 1.37.5；运行库差异由启动时的 ICU 解析和 SDL 路径发现处理。
