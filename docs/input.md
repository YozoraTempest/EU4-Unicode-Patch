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

原生搜索框的字节预算为 250。验收在专用进程中临时调整预算以触发小边界，完成后恢复原值；不改变 UI 定义或用户设置。原生记录见 [完整跟踪](evidence/native-editor-trace.jsonl)与[独立校验报告](evidence/native-editor.json)。DLL 指纹为 `defdc5e398c90e7b8a73ceb0c6273776a7fc24b8c36257dd2d2baf6434bb70ca`。

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

这项结果证明实际原生对象上的程序调用。它不证明物理键盘、中文输入法提交或候选窗口、剪贴板、多行、鼠标命中/拖选、撤销及自定义名称持久化已完成。

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

九项证据检查包括有效基线、半个字符选区、错误光标/锚点、无效通知、按键未处理、遗漏结束、重复及代理异常。SDL 证据检查另拒绝选区观察遗漏与禁止提交误清选区。
