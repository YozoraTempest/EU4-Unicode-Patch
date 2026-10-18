# Linux 原生适配

目标为 Steam EU4 1.37.5 Linux x86_64 原生游戏，发布包面向 glibc 发行版。Windows 与 Linux 共用同一项目、版本及九个 Unicode 核心库，分别生成 Windows DLL 和 Linux `libeu4_unicode.so`。平台构建边界见 [platforms.md](platforms.md)，原生接口依据见 [linux-native-profile.md](linux-native-profile.md)。

Linux 插件接入原生 YAML 本地化、脚本 BOM、BMFont 字库解析、UI 与地图字形绘制、SDL 文本提交及编辑控件，并处理未启用的同名模组抢占登记。它使用汉化模组已有的字体资源；正文换行复用共享 ICU 边界，保持原生字宽、颜色和行距。

四组功能的主路径现已实装并分组完成原生验收：IME 的真实预编辑、提交和双向选区，复杂 UI/地图文字，查找器与外交中文/拼音筛选，以及稳定文化 ID 的姓名顺序和真实 Steam UTF-8 状态载荷。九组查找器矩阵和复杂预编辑视口保留各自产物证据；最终产物另测外交四组查询、多区域高亮、真实姓名和地图索引绘制。范围与固定哈希见 [四组功能验收](linux-four-feature-validation.md)。更多控件、缩放、压力测试及完整发布矩阵仍未完成。

缺少的独立 Unicode 字形已接入 Pango/Fontconfig 栅格化、额外原生纹理页和字体绘制批次，原模组字形优先；真实界面验证了 é、Å、Ω、Ж、𠮷、彩色正文及强制换行。原生提示框的行长试探也按完整格式字素处理，避免将 UTF-8 续字节移入下一行；地图分页保留原生四角点与索引绘制格式。

复杂 UI 段落现已把共享 `ShapedParagraph` 连接到 Linux 原生图集、屏幕和缓存纹理绘制。真实游戏验证了 Arabic/Hebrew/Hindi/组合字符与原生中文、颜色及图标混排，以及两个复杂段落切换后缓存内容正确。原模组已有字形继续按原生指标绘制，临时 token 只在绘制/测量调用期间解析，避开原文中的 PUA 字符；长传输串通过原生零高度字形提交单词，不写入额外换行符。段落原生高度裁切、末尾空白及尺寸测量的一致性已有独立验收。

复杂编辑器以完整行的同一几何处理鼠标命中、视觉左右移动和光标像素位置，文档 affinity 保留到编辑器销毁，预编辑状态仍独立清理。原生剪贴板写入按完整 Unicode 标量过滤禁用字符，避免误删阿拉伯文及组合字符中的续字节；字节容量裁切对齐完整字素，行首插入的光标限制在实际文档内。固定产物实测通过 Arabic/组合/中文移动、17点双向鼠标扫描、禁用字符及追加/行首/中间/组合边界容量测试。后续原生测试补齐了多区域双向选区、复杂预编辑整行视口与地图 cluster；最终外交控件的选区保留实际视觉间隙。既有测试各自绑定原始 SO，更多控件/缩放和压力测试仍是发布前缺口。

多人视图从 Steam 填入的名称保留 UTF-8；原生名称清理中的 Latin-1 逐字节转写已在该视图的两个调用点修正。控制字符替换规则保持原意，不修改 Steam 账户名称。

## 构建和依赖

Arch：

```sh
sudo pacman -Syu --needed base-devel cmake ninja python git icu file binutils sdl2-compat pango cairo fontconfig noto-fonts noto-fonts-cjk
git submodule update --init --recursive
bash tools/ci-linux-core.sh
```

Ubuntu 22.04/24.04：

```sh
sudo apt install build-essential cmake ninja-build python3 git file binutils pkg-config \
    libicu-dev libpango1.0-dev libpangocairo-1.0-0 libfontconfig1-dev \
    libcairo2-dev libharfbuzz-dev libfreetype6-dev fonts-noto-core fonts-noto-cjk \
    gcc-12 g++-12
git submodule update --init --recursive
CC=gcc-12 CXX=g++-12 bash tools/ci-linux-core.sh
```

CI 在同一次运行中构建 Windows、Arch 和 Ubuntu，保存核心测试、Linux 预加载入口测试和产物依赖信息。`build/linux-package` 包含 `.so`、启动脚本、许可证、SHA256 和动态依赖清单。SafetyHook、Zydis 和 PicoSHA2 使用固定版本；挂钩只在游戏 `main` 之前、单线程状态下安装，失败时撤销本次安装并拒绝启动游戏。

插件依赖系统 glibc、libstdc++、Pango、Cairo、Fontconfig 和 ICU 运行库。ICU 的 C API 不再作为 ELF 的直接 `NEEDED` 依赖；启动时会在宿主机中寻找匹配的 `libicuuc` 与 `libicui18n` 主版本，并解析项目使用的 API。这样 Ubuntu 的 ICU 74、Arch 的 ICU 78 等版本可以使用同一插件，缺少 ICU 时会在 `eu4_unicode_linux.log` 中明确拒绝。SHA-256 使用固定版本的 PicoSHA2；游戏导出旧 OpenSSL 符号，因此插件不混用系统 OpenSSL 上下文。ELF 只导出加载入口，内部实现及静态依赖符号保持局部绑定。

## 加载

将 CI 的 Linux `tar.gz` 包完整解压到路径不含空白字符的独立目录，例如 `~/.local/share/eu4-unicode-patch`，保留 `.so` 与 `launch-linux.sh` 的相邻位置。`LD_PRELOAD` 使用空白字符分隔库路径；游戏自身的路径由命令参数保留，可以含空格。tar 包保存启动脚本的执行权限；GitHub 的原始 artifact ZIP 不保存该权限。Steam 启动选项可使用：

```sh
"/absolute/path/to/patch/launch-linux.sh" --steam "/absolute/path/to/Europa Universalis IV/eu4" -- %command%
```

Steam 模式在游戏目录直接运行指定的 Linux `eu4`，继承 Steam 客户端的 App ID、IPC 和运行库路径。额外游戏参数放在 `--` 之前，例如 `-debug`；`--` 后保留 Steam 的原始命令信息。命令行直接运行仍可使用 `launch-linux.sh GAME_COMMAND [ARGUMENT ...]`。

脚本通过 `LD_PRELOAD` 加载插件。插件只处理文件名为 `eu4` 的主程序，首先校验 ELF ABI 和完整 SHA256，随后校验每个挂钩及原生调用位置的指令。未知版本拒绝安装挂钩，原因写入游戏工作目录中的 `eu4_unicode_linux.log`。

启动脚本首先保留用户显式设置的 `SDL_DYNAMIC_API`，否则按包内 `lib/`、Ubuntu 多架构目录、`lib64`、`/usr/lib` 的顺序寻找 `libSDL2-2.0.so.0`。游戏内嵌 SDL 2.0.4 在已调查配置中没有建立可用的输入法上下文；SDL2 兼容库提供受维护的 SDL2 ABI。实测使用 Fcitx5 拼音、X11/XIM 和本地输入法服务。桌面必须先正确配置输入法；查找弹窗及多人过滤框中的候选位置、预编辑视口、下划线和高亮、取消和中文提交已有原生实测。SDL/XIM 的非空 start/length 反馈按高亮范围显示，不把它解释为额外的独立插入光标。

Steam 可能向游戏传入 `LC_ALL=C`，导致 XIM 候选可以显示却无法提交中文。启动器将明确的 `C` / `POSIX` 设置转换为宿主机提供的 `C.UTF-8`，保留 C 数字格式并启用 UTF-8 字符转换；其他明确设置保持原值。输入法环境变量仍使用桌面配置，Fcitx 的已验证配置包含 `XMODIFIERS=@im=fcitx`。

已调查的游戏是原生 Linux ELF64 x86_64、ET_EXEC，完整 SHA256 为：

```text
af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3
```

无需修改汉化名称或 dependencies。存在同名登记时，只有一个候选在原生启用列表中，插件才选择它；没有启用项或多个候选同时启用时保留原生选择并记录冲突。日志包含名称、描述符路径、启用状态与选择结果。

## 实测范围

游戏验证使用独立 runtime 和显式 `-userdir=/absolute/test/path`，保留可执行文件与插件哈希、描述符、启用列表、原版与补丁日志。Linux 的 `userdir.txt` 不能代替已验证的命令行参数。测试素材使用本地汉化副本，无需订阅或下载创意工坊内容。

测试必须检查原生登记、实际中文本地化、字体挂载及游戏画面；单独测试选择算法或启动器状态不够。虚拟机的软件渲染用于功能验证，真实 GPU 性能、完整战役、存档往返和其他字库格式仍需要各自的验证证据。目录和 ZIP 模组分别有双扫描顺序的真实挂载测试。最终 CI 与逐组结果见 [Arch 验证记录](linux-validation.md)。游戏运行时诊断只读，保留失败记录，不能以进入前端对象的时间代替主菜单完成渲染或持续运行。
