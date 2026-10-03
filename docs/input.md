# 原生 Unicode 编辑验收

目标版本为 Steam EU4 1.37.5 Windows x64。输入实验通过 `start-test.ps1 -ExperimentalInput` 显式启用，默认构建的普通启动不启用这些挂钩。

## 已验证的原生编辑器行为

在隔离游戏的外交国家搜索框中，程序在 UI 线程调用原生 SetText、插入和按键处理函数；每次操作后直接读取原生文本与字节光标。18 个案例通过：

| 类别 | 覆盖 |
| --- | --- |
| 11 个字素按键 | 中文、U+20000、字素内部光标、组合重音、旗帜、ZWJ 家庭 emoji、删除到空串及首位移动 |
| 4 个字节预算 | 中文、组合重音、ZWJ 序列及刚好容纳四字节字符的边界 |
| 2 个插入位置 | 首位与中间插入后的预算截断 |
| 1 个过滤规则 | `代俣俤俧👧` 保留；真正的 `§/£/¤/@/{/}/斜线/反斜线/引号` 按原生规则删除 |

原生搜索框的字节预算为 250。验收在专用进程中临时调整预算以触发小边界，完成后恢复原值；不改变 UI 定义或用户设置。原生记录见 [完整跟踪](evidence/native-editor-trace.jsonl)与[独立校验报告](evidence/native-editor.json)。本次编辑、选区和几何复测 DLL 指纹为 `de9f902f6f07f74ff6d4daca94152078afa1ce081a94a8a515b755fc2acde137`。

## 修正的两个独立问题

原生编辑器的 `153a080 / 153a450 / 153a850` 三条插入路径按字节预算截取前缀，可能留下半个字符或分离组合字素。补丁仅在这三处截取调用中将前缀长度向前对齐到 ICU 字素边界。保持字节预算及原生通知流程，其他字符串赋值调用不变。

原生输入过滤 `b19590` 把禁用字符表当作单字节集合。实测该框的表为 `22 a7 a4 a3 40 7b 7d 2f 5c`；其中 `a7` 会错误删除“👧”中的续字节，破坏 ZWJ 家庭序列。补丁只适配原生编辑器的 `1536c37` 调用，按 Unicode 标量与既有 Latin-1 禁用值比较，保留实际禁用字符的规则。其他过滤调用继续执行原函数。

## 重建与独立校验

```powershell
.\tools\start-test.ps1 -ExperimentalInput
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-editor.py --duration 600
```

进入法国测试战局，关闭显示事件，打开国家面板的外交页，再点击国家列表 Name 排序。脚本要求精确的隔离 EXE、启用的 DLL、输入开关与单一专用进程；完成或失败时关闭该进程后释放代理。

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-editor-trace.py private\native-editor-trace.jsonl
..\EU4MenuPatch\.venv\Scripts\python.exe tests\editor_evidence_tests.py
```

校验不依赖记录中的 `passed` 标志单独判断成功：它核对全部预期文本、按键、预算、字节光标、案例数量及完整结束事件，拒绝缺失、重复、错误结果和代理异常。

## SDL 完整提交与原生队列

SDL 文本事件最多携带 31 个 UTF-8 字节及终止符。补丁严格检查整个负载，将其放入原生 `0x58` 字节节点的 `+0x10` 文本区域，并在 `+0x30` 标记本补丁的完整提交。原生 `836f10` 对队列节点的整段复制已经核对；负载随节点拥有自己的存储，排队期间不持有调用栈或临时字符串指针。类型、文本类别及通知标志仍遵守原生结构。

原生事件路由 `14e9cb6` 把节点的文本成员交给 `15354e0`。该消费者仅识别补丁标记的有效负载；消费期间的同步插入调用 `1535615` 使用完整字符串。其他字符调用保持原有 ABI。过滤保留控件的两个原生禁用表，以及原生上下文条件下的七个特殊禁用字符；比较单位为 Unicode 标量。

原生插入还会按控件可见行数/高度执行回删。原来的内部回删通过单字节 Backspace 暴露半个字符，并反复发出变更通知。仅在插入函数的 `1536e51` 调用处改为完整字素擦除，再通过原生光标定位和行缓存更新几何状态。该内部循环不发出通知；原生插入结束后的一次普通通知保留。

像素截断接入后，该搜索框实际宽度 164、字体边距 14，可用宽度 150 像素；12 个 `A` 宽 144，13 个宽 156。旧查找器包含首个溢出字符，新查找器保留最长能容纳的完整前缀。高度为 15、字体行高为 16 时，原生回删保留 12 个 `A`，原生行缓存恰为这一行。SDL 原始记录保留完整前缀宽度、控件限制与最终行缓存，独立校验核对它们；不据此扩大到其他多行控件。

隔离游戏中的 26 个合成 SDL 事件穿过实际轮询入口、两次队列复制及聚焦的国家搜索框，覆盖 ASCII、中文、U+20000、组合重音、旗帜、ZWJ、续字节碰撞、真实禁用字符、31 字节负载、控件范围回删、字节预算和非法编码。五个新增案例通过原生函数准备选区，再投递 SDL 事件，验证生僻字、ZWJ、中文、组合重音替换，以及全禁用提交保留原文本、光标和选区。所有被接受的负载只插入一次，监听器只收到一次最终有效文本；全禁用提交、非法编码、无终止符与空提交不触发插入或通知。证据见 [SDL 原始记录](evidence/native-sdl-input.jsonl)与[独立报告](evidence/native-sdl-input.json)。

```powershell
.\tools\start-test.ps1 -ExperimentalInput
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-sdl-input.py --duration 600
```

进入隔离战局的外交页，点击搜索框取得焦点，然后在另一终端执行：

```powershell
[IO.File]::WriteAllText('D:\Astra-Paradox\repos\EU4UnicodePatch\private\sdl-input-arm.txt','run')
```

完成或失败时脚本先关闭专用进程。记录观察原生插入函数内部，避免入口跟踪器替换返回地址而改变补丁的调用范围。原生队列是异步消费的，脚本在投递后等待后续 UI 帧，不能用投递函数返回时的编辑框状态判断成功。

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-sdl-input.py private\native-sdl-input.jsonl
..\EU4MenuPatch\.venv\Scripts\python.exe tests\sdl_evidence_tests.py
```

SDL 对完整字素可能跨事件拆分；这次结果证明每个有效 SDL 事件的完整提交，不证明整个输入法组合事务已整合。[SDL 官方结构说明](https://wiki.libsdl.org/SDL2/SDL_TextInputEvent)和[轮询线程要求](https://wiki.libsdl.org/SDL2/SDL_PollEvent)是事件布局和测试入口的依据；游戏私有队列与控件 ABI 来自该精确 EXE 的指令和运行观察。

## 尚需接入和验收

这项结果证明实际原生对象上的程序调用。它不证明物理键盘、中文输入法提交或候选窗口、系统剪贴板转换、多行、鼠标命中/拖选、撤销及自定义名称持久化已完成。

SDL 合成事件不能替代物理输入法、候选窗及预编辑的实测。单行选区已经接入原生边界，其他选择与光标路径仍需验收。当前原生证据只覆盖该单行搜索控件，其他编辑器入口仍需逐一验证。

## 单行原生选区

原生 `15366c0` 对 Shift 的编码为 `4`，直接处理 Shift＋Home/End，但没有连接 Shift＋左右键。补丁将这两个按键连接到现有的 `1538560/1538670` 选区动作；动作仍由游戏维护锚点、选区文本和选择状态。

原生左右移动 `15384d0/15385a0` 改为一次抵达完整字素边界，保留各自的几何更新和选择清理。选区构造 `153b170` 对单行 UTF-8 端点向外对齐，保留前后方向；折叠的字素内部端点向前对齐且保持折叠。原生选择删除和插入继续执行游戏的流程。适配仅用于已经确认的单行布局；多行尚未据此声明支持。

18 组案例、27 个操作后的状态通过，覆盖左右选择、扩张/收缩、跨越锚点、Home/End、组合重音、旗帜、家庭 emoji、选择删除/替换、反向字素内部端点及选择清理。每一步检查文本字节、光标、锚点、选区内容与状态；[原始记录](evidence/native-selection.jsonl)与[独立报告](evidence/native-selection.json)保存完整结果。这些直接函数调用没有观察到外交变更通知；完整提交的最终通知另由上述 SDL 路径验证。

```powershell
.\tools\start-test.ps1 -ExperimentalInput
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-selection.py --duration 600
```

进入外交页后点击 Name 排序，再独立校验：

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-selection.py private\native-selection.jsonl
..\EU4MenuPatch\.venv\Scripts\python.exe tests\selection_evidence_tests.py
```

九项证据检查包括有效基线、半个字符选区、错误光标/锚点、无效通知、按键未处理、遗漏结束、重复及代理异常。SDL 的十三项证据检查另包括有效基线，并拒绝选区观察遗漏、禁止提交误清选区、缺失/错误像素约束、字体测量或实际行缓存。

## 原生像素定位与宽度截断

原生像素定位 `15361f0` 逐字节测量前缀。修复前，中文、U+20000、组合重音、旗帜与家庭 emoji 都能得到字素内部的字节光标；家庭序列一项有 93 个像素位置落在内部。宽度查找 `1537210` 也会比较不完整前缀。补丁先取得 ICU 字素边界，使用控件的真实字体、测宽函数和标志比较完整前缀；像素命中选择最近边界，等距离保留原生的左侧选择规则。

宽度截断返回能够容纳的最长完整字素前缀；首个字素本身过宽时，保留整个首字素，让原生行构建继续推进。查找不会在 CRLF 内分开。原生 ASCII 空格换行 `1539820` 的结果也向前对齐，避免拆开“空格＋组合重音”。零可用宽度曾使旧查找器在合成夹具上无响应；该观察不证明正常界面会触发这个前置条件。

11 组、727 个像素位置通过，覆盖 ASCII、中文、生僻字、组合字符、旗帜、家庭序列、续字节碰撞、带重音的空格、空串，以及首个生僻字/家庭序列过宽。探针在 UI 线程调用原生定位、截断与单词边界函数，用实际外交搜索字体测宽；文本字段使用独立值夹具，不改写显示中的搜索内容。每个整数像素结果与声明的字素边界及原生前缀宽度独立比对。证据见[原始记录](evidence/native-editor-geometry.jsonl)与[独立报告](evidence/native-editor-geometry.json)。18 个编辑案例和 18 组、27 个选区状态在本次 DLL 上复测通过。

这项证据覆盖原生几何函数，尚不证明物理鼠标点击/拖选、其他 GUI 行缓存、多行编辑或复杂文字 shaping 已经验收。物理输入法观察尚未收到实际操作记录，保持待验收。

```powershell
.\tools\start-test.ps1 -ExperimentalInput
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-editor-geometry.py
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-editor-geometry.py private\native-editor-geometry.jsonl
..\EU4MenuPatch\.venv\Scripts\python.exe tests\geometry_evidence_tests.py
```

进入外交页触发探针。正常完成或失败时先关闭专用进程，再卸载跟踪器；`--keep-open` 可用于同时完成编辑/选区回归，错误仍立即结束专用进程。十一项证据检查拒绝内部/错误光标、部分前缀、失去推进、组合空格截断、缺失像素、结束事件或重复案例、错误字体契约及代理异常。

## 原生 UTF-8 粘贴

SDL 的 `SDL_GetClipboardText` 返回需要调用方释放的 UTF-8 缓冲。本机原生粘贴 `1539240` 复制了内容，却没有调用 `SDL_free`，还在读取前清除了活动选区。实验输入补丁持有 SDL 缓冲直至原生插入结束，并在所有退出路径释放；先检查完整 UTF-8 与 32,000 字节上限，再执行原有字体转换和禁用字符过滤。接受的文本交给原生插入动作替换选区，拒绝的提交保留文本、光标和选区。

字体转换 `15a04f0` 的禁用表包含 Latin-1 `a4`。它原来会删除“俤”的 UTF-8 续字节。补丁只在粘贴字体转换期间、且返回地址为 `15a05ad` 的过滤调用中按 Unicode 标量比较，保留实际 `¤` 禁用规则。其他字体转换调用保持原有行为。插入预算、完整字素回删及最终通知继续使用前面已经验收的原生路径。

16 个真实单行控件上的原生 Ctrl-V 分发案例通过，包括 32 字节文本（超过一个 SDL 文本事件）、基本汉字、U+20000、重音、旗帜、家庭序列、字体过滤续字节碰撞、字节预算、正反向选择替换，以及全禁用、空、非法代理项编码和超长提交保留选择。每个案例分配、读取、释放恰好一次；接受的文本一次完整插入、一次最终通知，拒绝的文本不触发插入或通知。见[原始记录](evidence/native-clipboard.jsonl)和[独立报告](evidence/native-clipboard.json)。

探针在 UI 线程直接调用原生 Ctrl-V 分发器，将 SDL 动态 API 表的文本来源替换成私有测试提供者，使用真实 `SDL_malloc/SDL_free`。它不读取或改写系统剪贴板。嵌套调用会抑制普通 Frida 监听回调，因此插入与通知通过转发观察器记录后调用实际原生函数；不在探针中实现插入、过滤或通知。插入观察器地址来自与运行 DLL 指纹一致的构建 MAP，并检查可写数据节；记录保留 DLL/MAP 指纹。正常结束或失败时先关闭专用实例，再释放探针。

```powershell
.\tools\start-test.ps1 -ExperimentalInput
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-clipboard.py --duration 600
```

进入隔离战局的外交页触发案例，然后独立校验：

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-clipboard.py private\native-clipboard.jsonl
..\EU4MenuPatch\.venv\Scripts\python.exe tests\clipboard_evidence_tests.py
```

十三项证据检查覆盖有效记录、漏释放/重复释放、半字符插入、部分通知、未替换选择、拒绝输入误清选择、非法文本插入、字体过滤损坏、缺失案例/结束、观察器 MAP 元数据和代理异常。这项验收不证明物理 Ctrl-V、系统剪贴板编码转换、复制/剪切或输入法已通过。[SDL 获取文本](https://wiki.libsdl.org/SDL2/SDL_GetClipboardText)与[设置文本](https://wiki.libsdl.org/SDL2/SDL_SetClipboardText)的官方契约说明 UTF-8 编码及所有权；游戏函数和控件范围来自精确 EXE 的指令与运行证据。

## 原生复制、剪切与再粘贴

在同一个真实单行控件上，另用八类文本分别执行原生 Ctrl-C 或 Ctrl-X，再执行 Ctrl-V，共 16 个往返案例。选区包含 ASCII、中文、生僻字、组合重音、旗帜、家庭 emoji、续字节碰撞和超过单个 SDL 事件的 32 字节文本；正反方向选择均覆盖。复制保留原文本与选择，剪切恰好删除完整选择并留下 `AZ`；再粘贴恢复原文且只通知一次最终状态。

原生复制 `15395b0` 和选择剪切 `1539090` 已使用 UTF-8 调用 SDL，无需额外编码转换。探针将 `SDL_SetClipboardText` 的动态 API 表入口替换为私有接收者；再粘贴的来源使用这次实际捕获的字节，不能直接拿预期夹具重建结果。每个案例只写入一次、只读取一次、真实分配与释放各一次。见[往返原始记录](evidence/native-clipboard-roundtrip.jsonl)和[独立报告](evidence/native-clipboard-roundtrip.json)。

直接分发复制/剪切时，没有观察到外交变更通知；验证仅要求其完整文本和选择状态，再粘贴的最终通知另外核对。这不能证明物理 Ctrl-C/Ctrl-X 的完整外层事件链，也不能证明系统剪贴板转换或其他控件。十项专用证据检查拒绝复制误改选择、剪切错误范围、捕获字节损坏、提前读取、重复写入、跳过剪切后的实际状态、半字符插入、漏通知及错误模式。

```powershell
.\tools\start-test.ps1 -ExperimentalInput
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-clipboard.py --roundtrip --duration 600
```

进入外交页后，专用探针自动执行并关闭独立实例，然后校验：

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-clipboard.py private\native-clipboard-roundtrip.jsonl --roundtrip
..\EU4MenuPatch\.venv\Scripts\python.exe tests\clipboard_roundtrip_evidence_tests.py
```

## 光标与 SDL 输入矩形

实验输入将正常原生绘制 `1534250` 之后的光标精灵位置传给 `SDL_SetTextInputRect`。位置包括原生文本起点、滚动、对齐和字体偏移；字体行高来自该控件实际字体，矩形限制在当前 SDL 窗口内。只接受同时满足原生聚焦标志和输入管理器活动回调的控件。同一控件、窗口和坐标不重复调用；原生聚焦 `1535250` 清除缓存，保证重新聚焦会刷新。

原生聚焦函数会立即调用一次绘制，此时父级变换仍可能是上一帧的值。实际观察中首次位置为 `(640,328)`，下一正常帧才是该搜索框的 `(328,485)`。补丁保留同步绘制，只延后 SDL 位置提交到正常帧；修复后的首次矩形为 `(328,485,1,16)`，没有中间错误提交。重新聚焦也只提交一次当前光标矩形。

九个案例包括空串、ASCII 起点/末尾、中文、U+20000、组合重音、旗帜、家庭 emoji 和续字节碰撞字符。专用探针只用原生函数准备文本和光标，等待自然 UI 绘制，并记录实际完成的 SDL 调用、调用线程、聚焦拥有者、精灵位置、前缀测宽和字体行高。九个结果都与绘制光标一致；失焦后没有调用；由实际鼠标重新聚焦后恰好刷新一次。范围为该真实单行控件、1280×720、GUI scale 1，不能推及其他控件或复杂文字排版。见[原始记录](evidence/native-ime-rect.jsonl)和[独立报告](evidence/native-ime-rect.json)。

游戏报告的内置 SDL 版本为 2.0.4。[该版本 Windows 输入法实现](https://github.com/libsdl-org/SDL/blob/release-2.0.4/src/video/windows/SDL_windowskeyboard.c)使用输入矩形定位组合窗口，其候选 UI 和 TSF 处理与新版 SDL 不同；不能直接添加新版输入法 UI 提示项就宣称解决。这里验收的是 [SDL 输入矩形接口](https://wiki.libsdl.org/SDL2/SDL_SetTextInputRect) 的真实调用，物理候选窗、预编辑和输入法提交仍需实际输入法操作确认。

```powershell
.\tools\start-test.ps1 -ExperimentalInput
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-ime-rect.py
```

进入隔离战局的外交页并点击搜索框，然后在另一终端启用案例：

```powershell
[IO.File]::WriteAllText('D:\Astra-Paradox\repos\EU4UnicodePatch\private\ime-rect-arm.txt','run')
```

看到 `ime-rect-awaiting-refocus` 后重新点击搜索框。正常结束或失败时先关闭专用进程，再释放跟踪器。校验器及十三项证据检查只依赖 Python 标准库和声明的夹具，不需要 Frida：

```powershell
python tools\verify-ime-rect.py private\native-ime-rect.jsonl
python tests\ime_rect_evidence_tests.py
```
