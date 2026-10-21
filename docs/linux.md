# Linux 原生适配

目标为 Steam EU4 1.37.5 Linux x86_64 原生游戏，发布包面向 glibc 发行版。Windows 与 Linux 共用同一项目、版本及九个 Unicode 核心库，分别生成 Windows DLL 和 Linux `libeu4_unicode.so`。平台构建边界见 [platforms.md](platforms.md)，原生接口依据见 [linux-native-profile.md](linux-native-profile.md)。

Linux 插件接入原生 YAML 本地化、脚本 BOM 与旧双字节词元导入、BMFont 字库解析、UI 与地图字形绘制、SDL 文本提交及编辑控件，并处理未启用的同名模组抢占登记。它使用汉化模组已有的字体资源；正文换行复用共享 ICU 边界，保持原生字宽、颜色和行距。

四组功能的主路径现已实装并分组完成原生验收：IME 的真实预编辑、提交和双向选区，复杂 UI/地图文字，查找器与外交中文/拼音筛选，以及稳定文化 ID 的姓名顺序和真实 Steam UTF-8 状态载荷。九组查找器矩阵和复杂预编辑视口保留各自产物证据；最终产物另测外交四组查询、多区域高亮、真实姓名和地图索引绘制。范围与固定哈希见 [四组功能验收](linux-four-feature-validation.md)。更多控件、缩放、压力测试及完整发布矩阵仍未完成。

缺少的独立 Unicode 字形已接入 Pango/Fontconfig 栅格化、额外原生纹理页和字体绘制批次，原模组字形优先；真实界面验证了 é、Å、Ω、Ж、𠮷、彩色正文及强制换行。原生提示框的行长试探也按完整格式字素处理，避免将 UTF-8 续字节移入下一行；地图分页保留原生四角点与索引绘制格式。

复杂 UI 段落现已把共享 `ShapedParagraph` 连接到 Linux 原生图集、屏幕和缓存纹理绘制。真实游戏验证了 Arabic/Hebrew/Hindi/组合字符与原生中文、颜色及图标混排，以及两个复杂段落切换后缓存内容正确。原模组已有字形继续按原生指标绘制，临时 token 只在绘制/测量调用期间解析，避开原文中的 PUA 字符；长传输串通过原生零高度字形提交单词，不写入额外换行符。段落原生高度裁切、末尾空白及尺寸测量的一致性已有独立验收。

复杂编辑器以完整行的同一几何处理鼠标命中、视觉左右移动和光标像素位置，文档 affinity 保留到编辑器销毁，预编辑状态仍独立清理。原生剪贴板写入按完整 Unicode 标量过滤禁用字符，避免误删阿拉伯文及组合字符中的续字节；字节容量裁切对齐完整字素，行首插入的光标限制在实际文档内。固定产物实测通过 Arabic/组合/中文移动、17点双向鼠标扫描、禁用字符及追加/行首/中间/组合边界容量测试。后续原生测试补齐了多区域双向选区、复杂预编辑整行视口与地图 cluster；最终外交控件的选区保留实际视觉间隙。既有测试各自绑定原始 SO，更多控件/缩放和压力测试仍是发布前缺口。

多人视图从 Steam 填入的名称保留 UTF-8；原生名称清理中的 Latin-1 逐字节转写已在该视图的两个调用点修正。控制字符替换规则保持原意，不修改 Steam 账户名称。

## 构建和依赖

Arch：

```sh
sudo pacman -Syu --needed base-devel cmake ninja python git icu file binutils sdl2-compat pango cairo fontconfig noto-fonts noto-fonts-cjk ttf-dejavu
git submodule update --init --recursive
bash tools/ci-linux-core.sh
```

Ubuntu 22.04/24.04：

```sh
sudo apt install build-essential cmake ninja-build python3 git file binutils pkg-config \
    libicu-dev libpango1.0-dev libpangocairo-1.0-0 libfontconfig1-dev \
    libcairo2-dev libharfbuzz-dev libfreetype6-dev fonts-noto-core fonts-noto-cjk fonts-dejavu-core \
    gcc-12 g++-12
git submodule update --init --recursive
CC=gcc-12 CXX=g++-12 bash tools/ci-linux-core.sh
```

Fedora 43/44（使用 DNF 的 x86_64 系统）：

运行通用包需要以下运行库和字体，无需安装编译器：

```sh
sudo dnf install python3 libstdc++ libicu pango cairo fontconfig sdl2-compat \
    google-noto-sans-fonts google-noto-sans-cjk-fonts dejavu-sans-fonts \
    google-noto-sans-arabic-fonts google-noto-sans-hebrew-fonts \
    google-noto-sans-devanagari-fonts google-noto-sans-thai-fonts
```

Fedora 按文字系统拆分 Noto 字体包；Latin 与 CJK 字体不包含天城文、泰文等字形。上面的字体列表覆盖中文及复杂段落测试使用的文字，避免缺字时落到没有墨迹的 `.notdef` 字形。

从源码构建时，再安装开发依赖并运行同一个构建脚本：

```sh
sudo dnf install gcc gcc-c++ cmake ninja-build git file binutils pkgconf-pkg-config \
    tar gzip findutils libicu-devel pango-devel cairo-devel fontconfig-devel \
    harfbuzz-devel freetype-devel
git submodule update --init --recursive
bash tools/ci-linux-core.sh
```

Fedora 使用同一个 Linux `tar.gz` 包，保留下文的 P 社启动器安装流程。CI 在 Fedora 43/44 容器中先安装运行依赖，下载 Ubuntu 22.04 任务生成的原始压缩包及加载器测试程序，检查归档与内部 SHA256，再验证预加载、ICU 初始化和启动器安装／修复／卸载；之后才安装编译器，构建并运行完整 Linux 测试。这样能够发现只在构建环境中存在的依赖，避免把 Fedora 本机重新编译成功当成通用二进制兼容的证据。

容器测试不包含真实 P 社启动器窗口、GPU 游戏画面、输入法桌面会话或完整战役；Fedora 的这些项目仍需实际桌面验证。Steam Flatpak、Steam Linux Runtime 和不可变桌面系统的运行环境也需分别验证。

CI 在同一次运行中构建 Windows、Arch、Ubuntu 和 Fedora，保存核心测试、Linux 预加载入口测试和产物依赖信息。`build/linux-package` 包含 `.so`、启动脚本、许可证、SHA256 和动态依赖清单。SafetyHook、Zydis 和 PicoSHA2 使用固定版本；挂钩只在游戏 `main` 之前、单线程状态下安装，失败时撤销本次安装并拒绝启动游戏。

插件依赖系统 glibc、libstdc++、Pango、Cairo、Fontconfig 和 ICU 运行库。ICU 的 C API 不再作为 ELF 的直接 `NEEDED` 依赖；启动时会在宿主机中寻找匹配的 `libicuuc` 与 `libicui18n` 主版本，并解析项目使用的 API。这样 Ubuntu 的 ICU 74、Arch 的 ICU 78 等版本可以使用同一插件，缺少 ICU 时会在 `eu4_unicode_linux.log` 中明确拒绝。SHA-256 使用固定版本的 PicoSHA2；游戏导出旧 OpenSSL 符号，因此插件不混用系统 OpenSSL 上下文。ELF 只导出加载入口，内部实现及静态依赖符号保持局部绑定。

## 保留 P 社启动器（推荐）

下载并解压 Linux `tar.gz` 包，使用以下任一种方式。两种方式都保留 **Steam → dowser → P 社启动器 → EU4**：在启动器中选择播放集、模组和 DLC，点击开始游戏后才加载补丁。补丁目录和游戏目录均可包含空格。

### 方案一：安装一次，Steam 启动选项留空

退出游戏和 P 社启动器，使用终端运行：

```sh
bash "/absolute/path/to/patch/install-paradox-launcher.sh" "/absolute/path/to/Europa Universalis IV"
```

安装和卸载需要 Python 3，无需管理员权限。安装器备份原始 `launcher-settings.json`，只把 `exePath` 改为 `./.eu4-unicode/eu4-unicode-game.sh`，保留 `exeArgs` 和其他配置。随后清空 EU4 的 Steam 启动选项，照常从 Steam 和 P 社启动器进入游戏。

### 方案二：填写 Steam 启动项，自动恢复集成

将下面一整行填入 Steam → EU4 → 属性 → 通用 → 启动选项：

```sh
bash "/absolute/path/to/patch/start-paradox-launcher.sh" "/absolute/path/to/Europa Universalis IV" -- %command%
```

此脚本先安装或恢复集成，再原样执行 Steam 的 `%command%`，不向 dowser 或 P 社启动器预加载补丁。它不会跳过 P 社启动器，也不会添加 `-skiplauncher`。请替换两个目录路径，保留双引号、`--` 和 `%command%`。

### Steam 验证完整性和卸载

Steam 验证完整性可能恢复 `launcher-settings.json`。方案一需重新执行安装命令；方案二会在下次 Steam 启动时恢复。游戏更新后的可执行文件仍须通过补丁版本检查。

卸载前退出游戏与 P 社启动器，运行：

```sh
bash "/absolute/path/to/patch/uninstall-paradox-launcher.sh" "/absolute/path/to/Europa Universalis IV"
```

卸载恢复备份中的游戏入口，保留启动器后来修改的其他配置、原始设置备份及许可证。使用方案二时还需清空 Steam 启动选项，以免下次启动重新安装。

### 运行方式与诊断

安装目录是游戏目录中的 `.eu4-unicode/`。游戏包装器先进入游戏目录，然后使用相对路径 `./.eu4-unicode/libeu4_unicode.so` 预加载补丁，以避免 `LD_PRELOAD` 把安装路径中的空格当成库名分隔符。启动器传入的全部游戏参数保持原值。

补丁只处理文件名为 `eu4` 的主程序，首先校验 ELF ABI，记录完整 SHA256 作为诊断，再检查主程序的可读可执行映射和每个挂钩及原生调用位置的指令。与 Windows 一样，哈希变化本身不拒绝加载；程序布局或关键指令不兼容时拒绝安装挂钩，原因写入游戏目录中的 `eu4_unicode_linux.log`。正常加载时包含 `Linux native hooks enabled`。

启动脚本保留用户显式设置的 `SDL_DYNAMIC_API`，否则寻找可用的 SDL2 库路径。明确的 `LC_ALL=C` / `POSIX` 会转换为 `C.UTF-8`。输入法服务仍须由桌面正常配置；现有真实输入测试使用 Fcitx5、X11/XIM。Steam Linux Runtime 容器的运行库版本仍需单独验证；发行版核心测试不代表容器、启动器画面或全部游戏交互已经验证。

## 文本编码、中文格式与全选

Linux 本地化 `.yml` 和 GUI、文化、历史等文本脚本均接受无 BOM 或带 BOM 的 UTF-8。本地化解析器需要的签名只补到临时解析缓冲区，不改写模组文件；脚本只消耗文件开头的 BOM，正文中的 U+FEFF 保留。旧汉化本地化的 CP1252 包装转义和脚本词元中的原始双字节转义分别在导入时转为 UTF-8。

脚本转换保留词元类别、回看状态与引擎内存所有权；截断转义和转换后超过固定缓冲区的词元会记录错误并停止该次解析。支持旧转码不等于允许格式错误，例如本地化键重复的 `:0:0` 仍需由模组修正。

中文日期与年月调整为年在前，中文战役与围城标题调整为“地名之战／之围”；原生非中文格式保持不变。`Ctrl+A` 全选原生文档，包括中文、组合字符及多行文字；输入法组合期间不改变真实文档选区。Windows 补丁也提供 `Ctrl+A`。

## 自定义拼音词典与搜索配置

安装后编辑游戏目录 `.eu4-unicode/config.ini`，重启游戏生效：

```ini
[search]
typo_tolerance=1
fuzzy_pinyin=0
```

`typo_tolerance` 控制拼音输入容错，`fuzzy_pinyin` 控制模糊音；`0` 关闭、`1` 开启，默认值与 Windows 相同。自定义词典放在同一目录的 `pinyin.txt`，例如：

```text
重庆: chong qing | zhong qing
𠮷野: ji ye
```

配置和词典支持 UTF-8 及 UTF-8 BOM，两平台共用解析器；配置另保留 Windows 已有 UTF-16LE BOM INI 支持。直接预加载 `.so` 时，两文件放在该 `.so` 旁。安装器会复制补丁包中提供的配置与词典，重复安装或修复启动器集成时保留玩家已有文件。词典错误会记录到补丁日志，不覆盖已载入的有效读音。

## 可选：跳过 P 社启动器的直接启动

仅在明确需要直接进入游戏时使用原有的 `--steam` 模式：

```sh
"/absolute/path/to/patch/launch-linux.sh" --steam "/absolute/path/to/Europa Universalis IV/eu4" -- %command%
```

此模式添加 `-skiplauncher`，直接运行指定的游戏。补丁解压目录不能含空白字符，游戏目录可以含空格。希望保留 P 社启动器时请使用前面的方案一或方案二。

已调查的游戏是原生 Linux ELF64 x86_64、ET_EXEC，完整 SHA256 为：

```text
af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3
```

无需修改汉化名称或 dependencies。存在同名登记时，只有一个候选在原生启用列表中，插件才选择它；没有启用项或多个候选同时启用时保留原生选择并记录冲突。日志包含名称、描述符路径、启用状态与选择结果。

## 实测范围

游戏验证使用独立 runtime 和显式 `-userdir=/absolute/test/path`，保留可执行文件与插件哈希、描述符、启用列表、原版与补丁日志。Linux 的 `userdir.txt` 不能代替已验证的命令行参数。测试素材使用本地汉化副本，无需订阅或下载创意工坊内容。

测试必须检查原生登记、实际中文本地化、字体挂载及游戏画面；单独测试选择算法或启动器状态不够。虚拟机的软件渲染用于功能验证，真实 GPU 性能、完整战役、存档往返和其他字库格式仍需要各自的验证证据。目录和 ZIP 模组分别有双扫描顺序的真实挂载测试。最终 CI 与逐组结果见 [Arch 验证记录](linux-validation.md)。游戏运行时诊断只读，保留失败记录，不能以进入前端对象的时间代替主菜单完成渲染或持续运行。
