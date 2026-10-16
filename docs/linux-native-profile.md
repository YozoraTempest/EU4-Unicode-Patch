# EU4 1.37.5 Linux 原生接口依据

本接口表只适用于 `docs/linux.md` 中完整哈希匹配的 Steam ELF。地址是 ET_EXEC 虚拟地址，不复用 Windows RVA 或 MSVC 对象布局。`src/linux/profile.cpp` 保存各位置的 16 字节原始指令；插件校验完整文件后，在创建任何挂钩前复核全部位置。

## 加载与符号边界

可执行文件导出旧 OpenSSL 接口，包括 `EVP_DigestInit_ex`（`0x24589f0`）和 `CRYPTO_free`（`0x241d920`）。插件通过固定版本 PicoSHA2 在自有上下文中计算完整 SHA-256，避免动态链接不同 OpenSSL 版本的上下文 API。ELF 导出表只保留 `__libc_start_main`，实现与静态依赖采用局部符号；CI 使用导出冲突 EVP 函数的真实 ELF 夹具，核对完整文件哈希和拒绝未知 ELF 的行为。

## 字符串与本地化

游戏的 GNU C++11 `CString` / `CUTF8String` 为 32 字节，数据指针在 +0、字节长度在 +8。本地化入口 `LocalizeYmlAddKey` 为 `0x23671a1`；原生 `SLanguageData::AddKeyValuePair` 为 `0x236b7f8`。插件保留有效 UTF-8，并通过原生插入函数复制键值；字符串成员及引擎容器不由插件释放。

## 字体与绘制

`CBitmapFont::InitPostRead` 为 `0x204dbb8`，`ParseFontFile` 为 `0x204de66`。字形表在字体 +0x100，原生字形记录为 16 字节。ASCII 保留原生存储，高 Unicode 字形由共享登记库持有；字体析构前释放登记，原生字符集析构仍处理原来的条目。

宽度、实际尺寸、需求尺寸、屏幕绘制、缓存纹理绘制、顶点缓冲及地图曲线文字分别接入完整 UTF-8 标量。地图 `AddNudgedNames` 的独立字形计数也必须按标量遍历，否则其栈上顶点缓冲尺寸会与实际写入不符。原生 256 字节暂存区按完整 UTF-8 块刷新，保持原生输出容量和宽度累计。

Linux `CTextureHandler::LoadTexture` 在 `0x22a7edb` 拒绝达到 16 MiB 的文件。修改仅在 `CBitmapFont::InitPostRead` 的线程局部上下文中跳到原生读取流程 `0x22a7efc`。原生分配、DDS 检查、GPU 创建及释放保持原样，其他纹理加载不受影响。没有修改模组字体文件或给特定工坊 ID 特判。

正文换行由独立的 `src/linux/text_layout.cpp` 接入共享 `FormattedText` / ICU 边界及有界缓存。`RenderToScreen`（`0x20519f8`）在 `0x2052239`、`RenderToTexture`（`0x204e886`）在 `0x204f0f6` 判断当前词段是否可以提交。两处均已累计当前字形宽度，使用当前标量最后一个字节之后的断行边界；绘制仍由原生代码执行。屏幕绘制的逐字紧急换行不拆开 ICU 判定不可分割的 Unicode 片段。

`GetActualRequiredSize`（`0x2055f92`）使用相同的词段提交边界；`GetHeightOfString`（`0x2053b88`）同时补齐 Unicode 标量字形查询和词段宽度清零。`GetRequiredSize`（`0x20565aa`）与 `GetActualRealRequiredSizeActually`（`0x2056d66`）的已核实截取调用点只保留完整的格式化字素前缀。绘制和这些测量入口使用相同的临时格式标记规范化：UTF-8 的 `§`、`£`、`¤` 转成原生解析器使用的单字节引导符，原始本地化和编辑文档不变。

`CString::GetSubstring`（`0x254c6ae`）实际先把第三参数限制到源串长度，再减去第二参数作为 `std::string::substr` 的长度，因此其整数参数为 `[begin,end)`。挂钩必须分别对齐两个绝对位置，不能把结束位置当作长度。`GetActualRealRequiredSizeActually` 的三个布尔参数依次为直接截断、输出显式换行、解析格式，最后的 `int*` 输出像素高度；高度超限时包含已经尝试的下一行及两倍纵向边距。`bool*` 只在实际添加省略号时置位。复杂段落由独立 `paragraph_fit.cpp` 使用同一字体布局裁切，返回逻辑源串；候选省略前缀重新排版，绘制 token 不进入输出字符串。普通 bitmap 输入继续由原生入口处理。

上下文由函数调用栈持有，嵌套调用结束后恢复上一层；缓存只持有自有文字和边界数据。传给原生只读函数的 GNU 字符串视图在整个调用期间有效，原生输出字符串继续由引擎赋值和析构。ICU 的强制换行边界在换行符之后，原生换行分支却直接提交剩余词段，因此 `native_wrap_after` 还在 CR/LF 或输入结束前提交待测词段，只跨过实际颜色指令；相同判断用于屏幕、纹理和高度测量，避免最后的中文词组及标点越框。这些普通 bitmap 边界与下述复杂段落路径分别处理。

提示框与编辑控件的行拆分还经过 `CTextBuffer::GetLineCharSize`（`0x20d6b06`）。其原生二分及线性收尾按字节测量前缀，可能返回 UTF-8 字符中间的位置，后续 `GetTextAsRows` 会从续字节创建下一行。Unicode 输入改为在共享 `FormattedText` 的完整字素/格式指令前缀中搜索，宽度和 padding 仍调用当前原生字体虚表的 `+0x68`、`+0xb0`；保留原控件宽度和格式标志。优先采用已测量范围内的 ICU 断行机会，长词必要时按完整字素拆分，过窄控件至少消耗一个完整字素。原生行对象的构造、空间处理和所有权不变，原始行串未被规范化或复制到插件持久状态。纯单字节输入保留原入口。

动态缺字由独立的 `src/linux/font_pages.cpp` 与 `font_draw.cpp` 处理。`InitPostRead` 在调用 `ParseFontFile` 前写入纹理 ID 和尺寸；解析返回后登记 glyph table、texture manager、ID、原页尺寸和字号。既有 BMFont/Unicode registry 字形优先，缺字通过共享标量栅格化生成自有记录及额外 CPU 页。原模组的纹理像素不变。分页 CPU+GPU 预留共限 256 MiB，顶点影子共限 64 MiB。

已核实原生纹理创建 `0x229f203` 的 format 11 为 RGBA8/BGRA/unsigned byte，type 0 为 2D；更新 `0x22adbb4`、释放 `0x22adc83`。纹理查询 `0x22a8a2e` 返回 TextureGFX 对象本身。`GfxSetTextures`（`0x22ae01b`）第三参数为纹理数组，第六参数为 sampler 数组。屏幕/纹理绘制及普通文字顶点每字形六顶点。地图曲线文字临时也生成六顶点，但原生上传压缩为四角点，使用原生每字形六索引的缓冲绘制；分页必须按四个物理顶点分批，并把首角点作为原生 base vertex，保留原索引绑定及 winding。页标签在临时几何最终 UV 写完后添加，在自有绘制批次中去掉并缩放到实际页尺寸。只记录核实过的字体上传调用点及已跟踪缓冲的后续更新；原纹理、sampler、stream buffer、stride 和 offset 在绘制后恢复。CPU 测量和纹理查询只登记页与关联，GPU 上传仅在真实字体绘制线程执行。上传前通过原生纹理设置选择 unit0，绘制后恢复纹理、sampler、顶点流及原 active texture unit，避免覆盖地图 fog 使用的其他纹理单元。纹理/字符集/graphics 生命周期分别释放自有页和 scratch buffer。

复杂 UI 段落由 `src/linux/paragraph.cpp` 的调用栈作用域接入 `font_pages.cpp` 自有缓存。`GetWidthOfString`（`0x2053f7c`）使用共享行进位规划；屏幕/纹理、实际尺寸与高度入口借用临时传输串。`FindColor(char,uint32&)`（`0x20519a2`）、字体虚表 `+0x100` 的图标宽度及 `+0x110` 的旗帜宽度均已按完整函数和两类字体虚表核实；Linux 货币/编号符号原生宽度分别为16/8像素。字库大小在 `+0x938`，缩放在 `+0x940`。

共享传输规划只替换明确记录字节位置的生成 token，分配时排除原始 PUA 字符。普通嵌套调用遮蔽外层查询；同字库的传输子串副本可以借用外层几何，其他字库不能。记录归段落所有，不写原生字符表；原始本地化、编辑文档及存档不保存 token。每个图集使用单调增长的 token 范围，避免原生屏幕顶点缓存混淆图块数相同的不同段落；图集存活期间不回收该范围，容量耗尽会记录诊断并保留已有文字路径。

单词暂存区分段使用原生零高度/零进位字形的词段提交分支，不移动画笔、不写入额外换行符。颜色、旗帜和图标指令保持完整。段落 CPU 缓存按8 MiB预估记账；淘汰不回收已有顶点可能引用的图集像素，打包失败在发布几何前回滚页数及游标。Pango 合并相邻相同 shape 属性时，一个 item 可以包含多个原生字形对象；后端逐个使用其真实源位置取得坐标，并验证对象覆盖范围。完整复杂裁切、编辑几何及地图 cluster 传输仍待验收。

## 模组登记与依赖

| 原生位置 | 用途 |
| --- | --- |
| `0x1fdee14` | manager 初始化，扫描之后读取启用列表 |
| `0x1fdf3ca` | ReloadMods，扫描描述符 |
| `0x1fe03a0` / `0x1fe054a` | AddModFromFile / 校验后、构造树条目前的捕获点 |
| `0x1fddea0` | 按名称取得现有登记对象 |
| `0x1fe5468` | 原生描述符赋值，复制字符串、容器与其他成员 |

第一次扫描时启用列表尚未就绪，因此只保存名称与路径的自有副本，在初始化返回后选择。重新扫描在扫描返回后选择，不保留跨扫描的原生对象指针。

实际资源挂载由 `AppendVerifiedToPath`（`0x1fde258`）收集候选，在 `0x1fde33f` 调用 `CPdxArray<CString>::FindIndex`（`0xeb5704`）将描述符 +0x90 与启用列表严格匹配。插件调用同一原生函数，其 CString 比较按长度与字节判断。`GetActiveModNames` 的路径拆分只属于名称列表显示，不能代替挂载规则；反斜杠或带前缀的路径不自动转换成可加载路径。

唯一启用候选在原生扫描中落后时，重新通过原生 AddModFromFile 构造描述符，使用引擎赋值更新原登记对象。树节点和名称键不变。插件不修改通用树插入辅助函数，也不修改依赖匹配、名称排序或 rank 计算；rank 在上述实际挂载流程中根据最终描述符的名称依赖计算。

多个同名候选同时启用时不扩展为加载多份；保留原生选择并明确记录冲突。独立于 Initialize / ReloadMods 的直接安装入口尚不属于此模块的已验证扫描范围。

## 输入与编辑

SDL 事件入口为 `0x1ff50b0`，文本分支为 `0x1ff52d7`。原生分支只接收 ASCII 或部分双字节 Latin-1；插件验证完整 32 字节 SDL 文本包，按 Unicode 标量进入原生输入队列。

`CInputEvent` 为 88 字节，队列通过 11 个 MOVSQ 复制完整对象。文本字段位于 +0x10，其后 3 字节为经调查的 POD 填充；鼠标字段始于 +0x14，事件类型在 +0x50。普通构造先清理填充，Unicode 标量只在文本事件中编码。读取元数据限定在完整 CInputEvent 内，不读取独立单字节 CTextInputEvent 之外的内存。

原生 `GetTextInputEvent` 解析标量后，`GetString` 使用游戏 CString 构造完整 UTF-8。CEditBox 的原生过滤、插入及通知保持原样。文本缓冲区主字符串在 +0x30，光标在 +0x54/+0x56；左移、右移、退格和删除使用共享 ICU 字素边界，已有选择由原生处理。

SDL 提交不等于 IME 预编辑。候选窗、组合态定位、鼠标命中精度、拼音搜索和动态字体排版需要独立验证或适配，不能由上述 ABI 推断支持。

独立 `ime.cpp` 接入维护中的 SDL2 ABI 的 `SDL_TEXTEDITING` / `SDL_TEXTEDITING_EXT`，预编辑只写入 `CEditBox::UpdateEditSprite` 在 `0x20669ff` 持有的自有绘制暂存串，真实文档、搜索查询和保存数据不接收未提交文字。扩展事件的字符串由同一 SDL ABI 释放。焦点切换、取消和提交清理组合态，结束输入后恢复原字符事件订阅。

候选区域需要实际屏幕位置。`GetBaseCursorPosition` 只包含父容器中的局部位置，不能作为弹窗内编辑器的最终坐标。原生光标精灵的虚表 `+0x178` 为 `C2dObject::GetScreenPosition`（基类实现 `0x2097208`），返回 XMM0 低64位的两个 float，包含父级变换与对齐。候选位置使用该结果，再沿用原生 `CGraphics::Convert2dCoordinateToScreen`（`0x207c56c`）转换。

`CTextBuffer` 虚表 `+0x1d0`（目标 `0x20d53cc`）返回缓存的光标像素位置，会根据当前行列复用结果，不能用于测量样例字符或预编辑前缀。候选区域高度直接调用当前字体虚表 `+0x70` 的行高方法，前缀宽度使用 `+0x68` 的字符串宽度方法；目标 bitmap 行高实现为 `0x20541d4`，保留字体缩放。SDL/XIM 的 start/length 可能表示高亮范围而非独立插入光标，不能仅凭 start 为0 宣称内部光标移动失败或通过。

IME 的主线程投影持有自有文字、完整格式字素视口和范围。每个实际候选子串均由当前原生字体测量，保持左边缘 kerning；长串保留活动尾段。预览仅替换原生绘制暂存串中的选区，原文档、选择及撤销状态保持原生所有权。空预编辑事件、失焦、销毁和提交清理投影及绘制快照。

独立 `ime_presentation.cpp` 在瞬时文字字体调用前后 `0x208c130` / `0x208c19d` 及缓存纹理提交前后 `0x20d9ad4` / `0x20d9ae6` 绘制高亮背景与下划线。后一个返回位置的 R13 已加 `0x288`，需还原对象身份。源字符串分别在 Instant `+0x1a0` 和 Texture `+0x220`；同步快照仅持有对象身份、自有文字和几何，回调对照实际活对象和原生源串后才绘制。沿用引擎已有的 compatibility GL 绘制设施，恢复 program、矩阵、current color、裁剪、blend 和各 legacy texture unit，并在最初32次真实绘制前后比较状态。

光标图片包含透明偏移，装饰与候选位置采用文字精灵的实际内容起点：屏幕位置、CEditBox border `+0x240/+0x244`、字体 padding `+0x30/+0x34`，Instant 另加 `+0x254` 的垂直偏移，最后经过原生坐标转换。原生光标更新仍在已验证调用点应用投影视口的宽度增量。

`CTextBuffer::SetCursorPositionFromPixels(String,Point)`（`0x20d5142`）原生只写入 row0/column，不负责选择与通知。Unicode 输入通过当前字体测量完整格式字素前缀，选择最近边界，平局取左；单字节路径保留原生实现。CEditBox 的非密码调用点 `0x2066902` 已先减掉文字精灵外框，插件在该处进一步扣除实际 border/font padding，其他 CTextBuffer 调用方不受影响。初始化不持有跨编辑器生命周期的原生字符串或缓冲区指针。

复杂文字由独立 `editor_layout.cpp` 将上述命中入口、左右移动和光标像素位置接入同一完整行的 `ShapedParagraph`。作用域由 `CEditBox::HandleInput`（`0x2066750`）、`SetCursorPixelPosition`（`0x206684c`）以及 IME 的两条原生光标刷新路径建立；密码编辑框会屏蔽嵌套的普通文字几何。作用域只借用本次调用的 buffer，退出即还原，不保存字体或原生行指针。

`GetCursorPositionInPixels()`（`0x20d4f5a`）、接受 CString 的重载（`0x20d53cc`）和 `GetStartSelectionPositionInPixels()`（`0x20d4b58`）均以 RAX 返回两个 uint16，低16位为 x、高16位为 y。插件保留原生 y、padding 和缓存更新约定，只用完整行的视觉插入位置替换 x；选区起点保留原生额外3像素。当前行由 `GetRowNo(int)`（`0x20d4d76`）按 sret ABI 构造，长行和短行均调用原生 CString 析构入口 `0xd95742`，不假设插件分配器与游戏相同。

左右键在原生函数完成 prologue、尚未按逻辑行首/行尾判断前接入（`0x20d7844`、`0x20d78de`），处理完整行中的视觉停止点后返回各自原生收尾（`0x20d7876`、`0x20d793d`），由游戏清理旧选择并发出通知。跨行使用原生 `GetNoOfRowsInWrappedString`（`0x20d4a38`）和 `GetRowNo`；前者返回换行符个数，即末行索引。双向边界的 trailing affinity 按对象身份登记，只保存自有文档快照和行列，文本或位置改变后不能复用。它属于文档光标状态，在焦点切换后保留，编辑框销毁时删除；受互斥保护，当前调用作用域仍使用 TLS 隔离借用的 buffer。

真实鼠标扫描曾复现 byte2/trailing 命中后光标从13像素跳到39像素。只读快照证实 affinity 记录在命中后被清空；将 TLS 改为共享记录仍能复现，因此不能把该缺陷归因于线程切换。IME 焦点清理不再丢弃普通文档光标 affinity，预编辑清理保持独立；CI 覆盖另一线程刷新、切换到其他编辑器后返回，以及销毁清理。原生结果必须另外核对。

CI 中的 `linux_editor_layout` 测试运行实际作用域和命中适配代码，以 Pango 几何验证双向字节偏移、1.5倍字体缩放、完整字素、密码嵌套与普通 bitmap 路径。原生行所有权、光标缓存、移动通知及画面仍须由真实游戏验证。多区域双向选区绘制、复杂预编辑视口和组合期间鼠标操作尚未由此实现。

`CClipboard::GetClipboard`（`0x1fdd640`）直接从 SDL UTF-8 剪贴板构造 CString，没有 Latin-1 转码。`Paste`（`0x20d8766`）随后通过 buffer 虚表 `+0xa8` 调用 `CTextBuffer::Write`（`0x20d5caa`）。该函数在 `0x20d5ce3` 对暂存串调用逐字节 `RemoveIf<SCharIsInString>`，禁用字符表位于 buffer `+0xb8`。真实原生玩家名称控件的表为 `22 a7 a4 a3 40 7b 7d 2f 5c`；例如 Alef U+0627 的 `d8 a7` 会丢失续字节 `a7`，组合音标及印地文字也会受影响。这与容量超限是两个独立问题。

独立 `editor_mutation.cpp` 仅替换 Write 的上述过滤调用，将 AddForbiddenChars 的单字节字符值与完整 Unicode 标量匹配。禁用的引号、斜杠、格式符等仍被删除，其他字符中的相同 UTF-8 字节得以保留。暂存串已由游戏构造，长度变化通过原生 `_M_replace`（`0x25a3f30`）完成，之后继续原生插入、选区、通知与析构。无效 UTF-8 保留原生路径；通用 CString 过滤函数不变。

`TryToAppendStringToText`、`AppendTextToString`、`InsertStringInToText` 中五处文档前缀裁切调用分别位于 `0x20d61e4`、`0x20d622d`、`0x20d6722`、`0x20d676b`、`0x20d68f5`。它们传入的 CString 均为 buffer `+0x30`，substring 参数为 `[begin,end)`。插件按现有 FormattedText 的完整字素与格式指令边界向下对齐 end，不改变 max-chars 的原生字节容量，也不接管构造、赋值、析构或后续光标计算。

Write 在行首插入非空文档的分支直接于 `0x20d5f33` 存入原输入字节数，其他分支才调用 CalculateCursorPosition。容量裁切后，原输入可能长于实际文档；该位置在有效 UTF-8 文档上将待存列号限制到实际长度，避免光标指向文档之外。

1.25 倍原生测试又确认事件位置为屏幕像素，而 `GetPositionAtScreen` 返回未缩放的文字精灵坐标。`0x2066869` 位于返回坐标后、原生减法之前；Unicode 非密码输入在这里调用 `CGraphics::ConvertScreenCoordinateTo2d(CPoint<int> const&)`（`0x207c508`），沿用引擎对 `graphics+0x4b0/+0x4b4` 的乘法和整数截断。该函数返回 RAX 内的两个 int，与浮点屏幕位置方法的 SSE ABI 区分。随后执行原生减法和已有内容边距修正，不自行推算 GUI 缩放，也不改变其他鼠标事件。

## 多人游戏名称

`PdxSteamGetUserName`（`0x2391b99`）和 `CSteamMatchmakingContext::GetLocalUserName`（`0x2218700`）返回原始 UTF-8。`CFrontendMultiplayerView::Show` 复制名称后，在 `0x1c6e747` 调用 `CString::RemoveControllerCharacters`，在 `0x1c6e751` 调用 `RemoveSpecialCharacters`。前者把单字节 `0xA7` 改为 `s`，会破坏包含该续字节的 Unicode 字符；后者逐字节执行 Latin-1 转写，会破坏完整 UTF-8 名称。

`src/linux/multiplayer_name.cpp` 只拦截这两个调用点。有效 UTF-8 按标量替换 `@`→`a`、`$`→`S`、`§`→`s`，保留其余 Unicode；需要改变长度时调用已验证的原生 `_M_replace`（`0x25a3f30`）更新已构造的 CString。后续对引号及斜杠的原生替换保持原样。非 UTF-8 输入继续经过原生清理。通用 CString 清理函数以及其他调用方不变，日志不记录账户名称。
