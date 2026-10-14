# Linux 原生适配

目标为 Steam 的 EU4 1.37.5 Linux x86_64 原生程序，测试基准为 Arch Linux。

当前 Linux 构建只提供共享 Unicode 核心库与自动测试，不生成能够加载到 EU4 的插件。UTF-8、字素和换行边界、格式化文本、编辑、拼音搜索、字形登记和绘制批次逻辑使用 Linux ICU 编译。测试中沿用的原生事件数据样例仍属于已有 Windows ABI 契约，不能据此认为 Linux 游戏接口已经适配。

在 Arch Linux 中运行：

```sh
sudo pacman -Syu --needed base-devel cmake ninja python git icu
git submodule update --init --recursive
bash tools/ci-arch-core.sh
```

CI 使用 Arch 用户空间并记录发行版、内核和全部软件包版本。图形、输入法和原生游戏实测需要独立 Arch 桌面环境，容器内的核心测试不能替代它们。

取得 Linux 游戏程序后，先记录 ELF 架构、Build ID、哈希、动态依赖、符号及本机实际版本，再调查函数边界、调用约定、引擎字符串和容器所有权。Windows RVA、PE 检查和 MSVC 对象布局不能直接用于 Linux。字体排版、绘制、加载与输入法接口在对应原生调用调查完成后实现。

实测使用独立游戏 runtime 与 userdir，保留原版和补丁日志、测试描述符和启用列表。首先建立原生无补丁对照，再验证中文 UI 与地图、搜索、输入、存档路径、目录／压缩包模组及同名登记。虚拟机的软件渲染只能用于功能验证，帧率和设备性能需要真实 GPU 的 Arch 环境另行检查。
