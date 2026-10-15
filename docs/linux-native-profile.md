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

上下文由函数调用栈持有，嵌套调用结束后恢复上一层；缓存只持有自有文字和边界数据。传给原生只读函数的 GNU 字符串视图在整个调用期间有效，原生输出字符串继续由引擎赋值和析构。ICU 的强制换行边界在换行符之后，原生换行分支却直接提交剩余词段，因此 `native_wrap_after` 还在 CR/LF 或输入结束前提交待测词段，只跨过实际颜色指令；相同判断用于屏幕、纹理和高度测量，避免最后的中文词组及标点越框。此模块不承担复杂文字 shaping。

提示框与编辑控件的行拆分还经过 `CTextBuffer::GetLineCharSize`（`0x20d6b06`）。其原生二分及线性收尾按字节测量前缀，可能返回 UTF-8 字符中间的位置，后续 `GetTextAsRows` 会从续字节创建下一行。Unicode 输入改为在共享 `FormattedText` 的完整字素/格式指令前缀中搜索，宽度和 padding 仍调用当前原生字体虚表的 `+0x68`、`+0xb0`；保留原控件宽度和格式标志。优先采用已测量范围内的 ICU 断行机会，长词必要时按完整字素拆分，过窄控件至少消耗一个完整字素。原生行对象的构造、空间处理和所有权不变，原始行串未被规范化或复制到插件持久状态。纯单字节输入保留原入口。

动态缺字由独立的 `src/linux/font_pages.cpp` 与 `font_draw.cpp` 处理。`InitPostRead` 在调用 `ParseFontFile` 前写入纹理 ID 和尺寸；解析返回后登记 glyph table、texture manager、ID、原页尺寸和字号。既有 BMFont/Unicode registry 字形优先，缺字通过共享标量栅格化生成自有记录及额外 CPU 页。原模组的纹理像素不变。分页 CPU+GPU 预留共限 256 MiB，顶点影子共限 64 MiB。

已核实原生纹理创建 `0x229f203` 的 format 11 为 RGBA8/BGRA/unsigned byte，type 0 为 2D；更新 `0x22adbb4`、释放 `0x22adc83`。纹理查询 `0x22a8a2e` 返回 TextureGFX 对象本身。`GfxSetTextures`（`0x22ae01b`）第三参数为纹理数组，第六参数为 sampler 数组。屏幕/纹理绘制及普通文字顶点每字形六顶点。地图曲线文字临时也生成六顶点，但原生上传压缩为四角点，使用原生每字形六索引的缓冲绘制；分页必须按四个物理顶点分批，并把首角点作为原生 base vertex，保留原索引绑定及 winding。页标签在临时几何最终 UV 写完后添加，在自有绘制批次中去掉并缩放到实际页尺寸。只记录核实过的字体上传调用点及已跟踪缓冲的后续更新；原纹理、sampler、stream buffer、stride 和 offset 在绘制后恢复。CPU 测量和纹理查询只登记页与关联，GPU 上传仅在真实字体绘制线程执行。上传前通过原生纹理设置选择 unit0，绘制后恢复纹理、sampler、顶点流及原 active texture unit，避免覆盖地图 fog 使用的其他纹理单元。纹理/字符集/graphics 生命周期分别释放自有页和 scratch buffer。缺字分页不等同于完整 ShapedParagraph 的原生绘制支持。

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

## 多人游戏名称

`PdxSteamGetUserName`（`0x2391b99`）和 `CSteamMatchmakingContext::GetLocalUserName`（`0x2218700`）返回原始 UTF-8。`CFrontendMultiplayerView::Show` 复制名称后，在 `0x1c6e747` 调用 `CString::RemoveControllerCharacters`，在 `0x1c6e751` 调用 `RemoveSpecialCharacters`。前者把单字节 `0xA7` 改为 `s`，会破坏包含该续字节的 Unicode 字符；后者逐字节执行 Latin-1 转写，会破坏完整 UTF-8 名称。

`src/linux/multiplayer_name.cpp` 只拦截这两个调用点。有效 UTF-8 按标量替换 `@`→`a`、`$`→`S`、`§`→`s`，保留其余 Unicode；需要改变长度时调用已验证的原生 `_M_replace`（`0x25a3f30`）更新已构造的 CString。后续对引号及斜杠的原生替换保持原样。非 UTF-8 输入继续经过原生清理。通用 CString 清理函数以及其他调用方不变，日志不记录账户名称。
