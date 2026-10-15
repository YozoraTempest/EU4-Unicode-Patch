# Linux 原生适配

目标为 Steam EU4 1.37.5 Linux x86_64 原生游戏，基准环境为 Arch Linux。Windows 与 Linux 共用同一项目、版本及八个 Unicode 核心库，分别生成 Windows DLL 和 Linux `libeu4_unicode.so`。平台构建边界见 [platforms.md](platforms.md)，原生接口依据见 [linux-native-profile.md](linux-native-profile.md)。

Linux 插件接入原生 YAML 本地化、BMFont 字库解析、UI 与地图字形绘制、SDL 文本提交及编辑控件，并处理未启用的同名模组抢占登记。它使用汉化模组已有的字体资源。共享拼音搜索算法能够在 Linux 编译，但 Linux 搜索控件、动态字体与复杂文字排版、IME 预编辑尚未接入；Windows 的姓名顺序与 Steam 状态扩展也未移植。当前功能范围以本页为准。

## 构建和依赖

```sh
sudo pacman -Syu --needed base-devel cmake ninja python git icu file binutils sdl2-compat
git submodule update --init --recursive
bash tools/ci-arch-core.sh
```

CI 在同一次运行中构建 Windows 与 Arch，保存核心测试、Linux 预加载入口测试和产物依赖信息。`build/linux-package` 包含 `.so`、启动脚本、许可证、SHA256 和动态依赖清单。SafetyHook、Zydis 和 PicoSHA2 使用固定版本；挂钩只在游戏 `main` 之前、单线程状态下安装，失败时撤销本次安装并拒绝启动游戏。

当前插件在滚动版 Arch 上构建，依赖系统 glibc、libstdc++ 和 ICU。SHA-256 使用固定版本的 PicoSHA2；游戏导出旧 OpenSSL 符号，因此插件不混用系统 OpenSSL 上下文。ELF 只导出加载入口，内部实现及静态依赖符号保持局部绑定。其他发行版及旧 ABI 的兼容性需要另外验证。

## 加载

将 CI 的 Linux `tar.gz` 包完整解压到路径不含空白字符的独立目录，例如 `~/.local/share/eu4-unicode-patch`，保留 `.so` 与 `launch-linux.sh` 的相邻位置。`LD_PRELOAD` 使用空白字符分隔库路径；游戏自身的路径由命令参数保留，可以含空格。tar 包保存启动脚本的执行权限；GitHub 的原始 artifact ZIP 不保存该权限。Steam 启动选项可使用：

```sh
"/absolute/path/to/patch/launch-linux.sh" --steam "/absolute/path/to/Europa Universalis IV/eu4" -- %command%
```

Steam 模式在游戏目录直接运行指定的 Linux `eu4`，继承 Steam 客户端的 App ID、IPC 和运行库路径。额外游戏参数放在 `--` 之前，例如 `-debug`；`--` 后保留 Steam 的原始命令信息。该模式使用 Arch 主机依赖 ABI，与 Steam 的容器 ABI 分开。命令行直接运行仍可使用 `launch-linux.sh GAME_COMMAND [ARGUMENT ...]`。

脚本通过 `LD_PRELOAD` 加载插件。插件只处理文件名为 `eu4` 的主程序，首先校验 ELF ABI 和完整 SHA256，随后校验每个挂钩及原生调用位置的指令。未知版本拒绝安装挂钩，原因写入游戏工作目录中的 `eu4_unicode_linux.log`。

启动脚本同时使用 SDL 官方 `SDL_DYNAMIC_API` 入口加载 Arch 的 `/usr/lib/libSDL2-2.0.so.0`，保留用户显式设置的 SDL_DYNAMIC_API。游戏内嵌 SDL 2.0.4 在已调查配置中没有建立可用的输入法上下文；Arch `sdl2-compat` 提供受维护的 SDL2 ABI。实测使用 Fcitx5 拼音、X11/XIM 和本地输入法服务。桌面必须先正确配置输入法；候选窗位置和游戏内预编辑显示仍不属于已完成接口。

已调查的游戏是原生 Linux ELF64 x86_64、ET_EXEC，完整 SHA256 为：

```text
af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3
```

无需修改汉化名称或 dependencies。存在同名登记时，只有一个候选在原生启用列表中，插件才选择它；没有启用项或多个候选同时启用时保留原生选择并记录冲突。日志包含名称、描述符路径、启用状态与选择结果。

## 实测范围

游戏验证使用独立 runtime 和显式 `-userdir=/absolute/test/path`，保留可执行文件与插件哈希、描述符、启用列表、原版与补丁日志。Linux 的 `userdir.txt` 不能代替已验证的命令行参数。测试素材使用本地汉化副本，无需订阅或下载创意工坊内容。

测试必须检查原生登记、实际中文本地化、字体挂载及游戏画面；单独测试选择算法或启动器状态不够。虚拟机的软件渲染用于功能验证，真实 GPU 性能、完整战役、存档往返、压缩模组和多字库格式仍需要各自的验证证据。游戏运行时诊断只读，保留失败记录，不能以进入前端对象的时间代替主菜单完成渲染或持续运行。
