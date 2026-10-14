# Windows 与 Linux 共用结构

两个平台使用同一项目、同一个 `VERSION` 和同一套 Unicode 核心源码，分别构建原生二进制。Windows 玩家补丁使用 DLL；Linux 原生游戏需要 ELF 共享库 `.so`。Windows DLL 不能作为 Linux 原生游戏插件直接加载。

## 构建边界

顶层 `CMakeLists.txt` 根据目标系统选择平台构建入口：

- `cmake/shared-core.cmake` 集中定义八个共享库、依赖和拼音数据生成。
- `cmake/windows.cmake` 配置 Windows ICU 导入、MASM、原生适配、DLL、现有工具与测试。
- `cmake/linux-core.cmake` 配置上游 ICU、PIC、Arch 核心测试。当前不生成 Linux 游戏插件。

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
| 加载入口与指令挂钩 | version API 代理、MinHook、MASM | ELF 共享库加载与已验证的原生挂钩，待实现 |
| 可执行文件校验 | PE 映像、代码及内存权限检查 | Linux ELF 与目标函数校验，待实现 |
| 字体布局与栅格化 | DirectWrite | 原生字体后端，待实现 |
| 图集、纹理与绘制 | D3D9 与 Windows 游戏调用 | Linux 游戏图形调用，待实现 |
| 输入法与控件接入 | IMM、Windows 消息及已验证的 SDL 事件入口 | Linux 文本提交、预编辑及控件接入，待实现 |
| 模组登记 | Windows 原生容器、字符串和登记上下文 | Linux 容器与调用顺序单独复核后实现 |

共享 Unicode 语义，平台层负责转换原生参数和资源。指令地址、引擎字符串、容器成员偏移与析构规则不跨平台复用；不得用现有 Windows 数据样例代替 Linux ABI 验证。

## 当前支持状态

Windows 构建能够生成游戏补丁。Linux 当前通过 CI 编译并测试八个共享库及九个测试程序，尚无能够在 EU4 中提供 Unicode 功能的 `.so`。

两平台共用核心构建后，继续从已安装的 EU4 1.37.5 Linux 原生程序调查接口。发布插件的依赖 ABI 必须对照实际 Steam Linux Runtime；Arch 核心程序的测试结果不等于游戏插件可加载。

同一次 CI 运行同时验证 Windows 与 Arch。未来 Linux 游戏插件完成后，可由同一发布流程提供两个系统的原生产物；当前发布流程仍只提供 Windows 游戏补丁，不包装尚未实现的 Linux 插件。
