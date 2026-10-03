# 原生 Unicode 字形桥接

文本始终保存为 UTF-8；字形键使用完整的 32 位 Unicode 标量。游戏的原生字体对象仍保持原来的大小和 ASCII 指针表。U+0100 以上的记录由独立稀疏注册表持有，记录布局与原生 16 字节 glyph metrics 一致。

字体加载的查重、分配与存储入口已经接入注册表。主文字、按钮、位图测宽及地图路径使用同一个查找宏，因此四字节 UTF-8 解码出的码点能够取回对应记录。开源字体预留图集模式先按需生成未加载的标量字形；实际字体缺字或页容量不足时先取省略号，再取问号。旧静态图集仍按预收录范围查找；占位行为不改写源文本。

原生字体副本共享 ASCII 字形指针，使用 A 的指针识别同一图集的别名。字形由注册表持有，插入和散列表扩容不会使已经交给游戏的指针失效。原生拥有者的字体表销毁 `1594360` 会释放 256 个 ASCII 记录；补丁在其之前释放同图集的稀疏 Unicode 记录和未绑定记录，旧借用别名不再匹配活图集。

`fontpack.exe` 使用 DirectWrite 系统字体回退与 Direct2D/WIC 栅格化。它收集源文件中的字符，额外加入原生 Latin 范围与省略号，生成五种字号的 FNT/DDS。每页宽度为 1024，最高 8192，纹理为无压缩 A8R8G8B8；地图图集使用黑色，UI 使用白色遮罩。生成的系统字体纹理仅用于本机测试，不提交或打包分发。

当前基本夹具每种字号共 339 个字符，专用国家搜索夹具为 350 个字符，五张图集均报告缺字数 0。其中 U+20000 的字体来自系统回退。运行跟踪检查四字节码点进入原生绘制，并将原生 16 字节 glyph metrics 与生成 FNT 的记录核对；只检查非空指针会把占位字形误判为成功。

这个桥接支持收录字符的真实游戏显示。复杂文字必须以整段文本的 shaping、bidi、glyph run、cluster 与命中测试结果为单位接入；逐字符生成的图集仅适用于当前已验收的简单文字路径。固定页运行时字形缓存与上传现已复用此注册表和系统布局模块，详见 [开源字体验收](open-fonts.md)；多页分配仍需连接原生批次和缓存，而不改变文本编码。

## 完整 shaped run 与栅格接口

独立布局现在保留 DirectWrite 返回的实际字体对象、字号、视觉基线、书写方向、测量模式、每个字形的 advance/ascender offset，以及从 UTF-8 源范围到字形范围的 cluster 映射。字体对象随 run 共同持有，原布局销毁后仍能使用正确的回退字体；字形编号不再只有一个供诊断的字体族名称。

`rasterize_glyph_run` 直接栅格化这些已经排版的字形，不重新用源文本排版，也不逐码点重建。它返回灰度 alpha 与相对 `floor(baseline_x/y)` 的像素边界，保留基线的小数相位。RTL 字形可以位于基线左边；空白 run 保留前进宽度而不分配位图。栅格上限为单边 16,384 像素和 64 MiB alpha，超限、缺字体、非有限几何或不完整数组均拒绝。

独立测试核对全部源字节/字形的 cluster 覆盖、连字和组合符号、RTL 负左边界、灰度抗锯齿、原布局销毁后的字体寿命、偏移移动与基线相位；整数平移不改变本地 atlas 像素，改变四分之一像素相位会改变覆盖。22 个实际字形 run、七种系统字体的组合诊断图见 [shaped-run 输出](evidence/unicode-shaped-runs.png)。诊断图与 Direct2D 整段文本分别渲染，字形与方向经视觉核对；它们不是逐像素相同的渲染模式。

这项结果补全了 CPU 到纹理桥的布局数据与栅格输入，尚未连接到 EU4 的 GPU 纹理、批次刷新、裁剪、测宽和选区。原有位图路径仍承担已经验收的游戏绘制。整段 shaped run 的 GPU 接入、多页纹理、原位重载与游戏复杂文字仍待完成；单标量按需字形另在开源字体模式接入了固定原生图集。

```powershell
.\build\unicode_layout_tests.exe private\unicode-layout-reference.png private\glyph-run-dump
python tools\compose-glyph-runs.py private\unicode-layout-reference.png private\glyph-run-dump private\unicode-shaped-runs.png
```

组合工具需要 Pillow，仅根据实际导出的 mask 和基线组图；参考 PNG 只提供画布大小。依据为 Microsoft 的 [glyph run](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ns-dwrite-dwrite_glyph_run)、[UTF-16 cluster 描述](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ns-dwrite-dwrite_glyph_run_description)及 [glyph run analysis](https://learn.microsoft.com/en-us/windows/win32/api/dwrite_2/nf-dwrite_2-idwritefactory2-createglyphrunanalysis) 契约。

本次用更新后的 `fontpack` 重新生成五种字号，全部十份 FNT/DDS 与当前游戏夹具逐字节相同，见 [生成回归记录](evidence/shaped-run-validation.json)。正式游戏与正在进行的物理输入法观察没有改用新的独立栅格接口。

原生 Direct3D 9 纹理、顶点声明和缓存绘制入口的实际调查见 [GPU 接口记录](gpu-bridge.md)。该记录的首轮调查只确认状态与数据布局；后续固定图集的按需上传已经通过实际 GPU 读回。整段 run 的坐标/着色器、页批次和缓存失效继续待办。

## 原生字体生命周期

修复前，实际原生字体销毁后仍能从旧别名查到 U+20000 字形；这些稀疏记录没有随原生 ASCII 表清理。修复将清理连接到 `1594360`，保持原生纹理释放、监听器移除和 ASCII/kerning 表析构流程。

五种字号在同一个原生对象存储位置轮流构造、加载和销毁，共 64 次。测试使用原生构造 `1594490`、字体/纹理加载 `15953c0` 与析构 `15947e0`，在 UI 线程执行。加载每次增加一个图集、148 个 Unicode 记录；销毁后回到 45 个图集、6,660 个记录的原基线。ASCII A 的 40 个地址在 64 次循环中发生复用，新的中文、emoji 和 U+20000 指标均与本次五份 FNT 的指纹及 16 字节指标对应。原有来源字体的指针与指标始终保留。

证据：[完整原生跟踪](evidence/native-font-lifetime.jsonl)、[独立校验报告](evidence/native-font-lifetime.json)。循环使用专门新建的测试字体，未销毁游戏正在使用的字体；临时借用表只查询 Unicode 标量，不解引用已释放 ASCII 字形。九项证据检查拒绝失效图集仍可达、记录泄漏、旧世代尺寸、别名错误、活字体损坏、缺失/重复及代理异常。

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-font-lifetime.py
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-font-lifetime.py private\native-font-lifetime.jsonl
..\EU4MenuPatch\.venv\Scripts\python.exe tests\font_lifetime_evidence_tests.py
```

脚本自行启动精确的隔离 EXE，部署本次构建并关闭输入实验；已有测试游戏运行时拒绝启动。结束时先关闭专用进程，再释放代理。它要求当前构建的 MAP 文件和本机系统字体夹具。该结果证明原生对象寿命与新建重载，不证明正在显示的字体可以原位热重载；本段记录属于原静态字体夹具。后续开源模式的动态缓存、CPU/GPU 上传和 Reset 恢复见 [开源字体记录](open-fonts.md)；活字体原位重载、多页纹理与完整 GPU 内存预算仍需各自验收。
