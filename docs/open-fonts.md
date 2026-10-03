# 开源字库与运行时按需字形

当前主要字体为 Source Han Sans SC Regular（思源黑体简体中文），扩展汉字回退为 Plangothic P1 / P2 Regular（遍黑体）。两个项目的字体原文件均采用 SIL Open Font License 1.1；固定版本、上游提交、下载地址、文件长度和 SHA-256 见 [字体清单](../fixtures/open-fonts.json)，完整许可证及版权说明见 [第三方说明](../THIRD_PARTY_NOTICES.md)。三个原文件共 49,399,744 字节，约 47.1 MiB。

| 字体 | 固定发行版 | 实际 cmap 非零字形映射数 | 用途 |
| --- | --- | --- | --- |
| Source Han Sans SC Regular | 2.005R | 44,853 | 优先提供常用汉字、简体中文界面和自身覆盖的扩展汉字 |
| Plangothic P1 Regular | V2.9.5795 | 65,440 | 优先字体没有覆盖的扩展汉字 |
| Plangothic P2 Regular | V2.9.5795 | 42,152 | 后续扩展区，包括本次实际测试的 U+30000、U+323B0 |

选择依据是 [Adobe 官方发行版](https://github.com/adobe-fonts/source-han-sans/releases/tag/2.005R)的成熟中文界面字形，以及 [遍黑体项目](https://github.com/Fitzgerald-Porthmouth-Koenigsegg/Plangothic_Project)的扩展区覆盖。保持字体原文件和名称，不合并或修改字体；打包时携带原许可证。DirectWrite 按上述顺序加载进程私有字体集合，然后沿用系统回退处理其余字符。该操作不安装系统字体，也不修改字体注册表。

## 覆盖审计

`tools/audit-open-fonts.py` 用 fontTools 4.66.1 读取实际字体的 cmap，排除 glyph ID 0，再与官方 Unicode 17.0 的 `UnicodeData.txt` / `Blocks.txt` 比较；First/Last 形式的已分配范围会完整展开。三个字体映射的并集为 135,063 个标量，其中统一汉字基本区、扩展 A–J、兼容汉字及其补充区共 13 个块的 **102,998 个已分配字符全部有非零字形映射**。审计输入和字体均核对固定 SHA-256，详见 [覆盖报告](evidence/open-font-coverage.json)。

这证明所选字库的 cmap 覆盖；没有证明全部字符已在游戏中逐个显示，也没有验收 IVS、变体选择符、复杂文字 shaping 或单页容量。布局后端需要保留实际字体选择，不能只凭码点范围猜测字体。

```powershell
.\tools\fetch-open-fonts.ps1
python -m venv private\font-analysis
.\private\font-analysis\Scripts\python.exe -m pip install -r tools\requirements-font-audit.txt
.\private\font-analysis\Scripts\python.exe tools\audit-open-fonts.py
```

字体从固定上游发行版下载到 `private/open-fonts/`；已经校验的分发包 `fonts/` 文件会直接复用。Python 和 fontTools 只用于离线审计，DLL 运行不依赖它们。

## 省略号原因与修补

原系统字体夹具每种字号只预生成本地化中出现的 339 个字符。用户输入不在图集中的汉字时，原生查找会返回 U+2026 省略号；源 UTF-8 字节没有丢失。仅更换字体原文件不会让原静态图集自动增加字符。

`prepare-test.ps1 -OpenFonts` 用上述字体生成同样的初始字符，并为每种字号预留固定 2048×4096 的 A8R8G8B8 图集。缺失标量第一次出现时，`scalar_glyph` 使用既有 DirectWrite / Direct2D / WIC 后端生成 alpha 与原生指标；`native_font_atlas` 在初始字符区域之后分配固定坐标，注册稳定的字形指针并排队。测宽线程只操作 CPU 数据；GPU 上传在游戏自己的纹理查找路径中进行，沿用原生纹理绑定和绘制。

图集不扩大、不重排已有字形，因此原生缓存顶点中的 UV 仍有效。系统内存 staging 先从实际生成的 DDS 读取全部初始像素，然后只更新新字形区域。原生 font load `15953c0` 注册字体/纹理关系，texture lookup `16c3f10` 刷新上传；原生字体表析构 `1594360` 释放对应绑定。共享纹理的字体对象共享图集，稀疏字形注册表仍按原生字体身份持有稳定记录。

DLL 不持有 default-pool 纹理的 COM 引用。设备 Reset 时先清除上传身份；即使重建纹理复用了旧地址，下一次原生纹理查找仍从 CPU staging 恢复完整图集。SYSTEMMEM staging 随最后一个字体拥有者释放。当前每页 GPU 内存 32 MiB，第一次动态上传再增加 32 MiB CPU staging；五种字号最多各一页，约 160 MiB GPU，全部触发后约 160 MiB CPU staging，另计字形缓存和字体文件。尚未实现多页、淘汰或全局动态预算。

## 已完成的实际验收

独立文件字体测试核对九个样本的实际字体、非空指标与像素，并验证原布局/字体集合销毁后 retained run 的字体寿命。[诊断图片](evidence/open-font-fallback.png)来自独立程序。

真实 EU4 外交搜索框通过三个受控 SDL 完整提交：“中华人民共和国”“孔雀翡翠”“𠮷𰀀𲎰”。每次提交的 UTF-8、光标、两次原生队列复制、一次完整插入与一次最终通知均核对。实际绘制调用返回位置为 `159b151`；读取当前字体管理器的资源并确认其就是设备 stage 0 绑定的纹理，再从这张真实 GPU 纹理逐字读回区域。

14 个不同字符的原生尺寸/偏移/advance 和每一个 alpha 字节都与独立栅格参考一致，均未返回省略号指针。其中“中、人、国”是静态字符，另外 11 个是运行时新增字形；U+20BB7 来自思源黑体，U+30000、U+323B0 来自 Plangothic P2。该结果覆盖普通汉字和四字节扩展汉字的当前单行路径。见 [原始记录](evidence/native-dynamic-fonts.jsonl)、[GPU 校验报告](evidence/native-dynamic-fonts.json)和 [CPU 字形参考](evidence/dynamic-font-cpu.json)。

另一个真实 Direct3D 9 设备测试使用模拟原生字体/资源拥有者，核对工作线程只生成 CPU 字形、原生图集上传、后续上传不破坏旧区域、U+30000、设备 Reset 后恢复、纹理地址复用、字形指针稳定和拥有者释放。它验证设备恢复契约，不替代游戏窗口切换/缩放的实际长期回归。

```powershell
# 先退出隔离测试游戏，再准备字体；默认仍保留原系统/工坊字体模式。
.\tools\prepare-test.ps1 -OpenFonts
.\build\open_font_tests.exe private\open-fonts private\open-font-fallback.png
.\build\native_font_atlas_tests.exe private\test-mod private\open-fonts
python tools\verify-dynamic-fonts.py docs\evidence\native-dynamic-fonts.jsonl docs\evidence\dynamic-font-cpu.json --report private\verified-dynamic-fonts.json
python tests\dynamic_font_evidence_tests.py
.\tools\start-test.ps1 -ExperimentalInput
```

重建真实游戏记录时，先用普通启动脚本进入测试战局并打开外交搜索框，在另一个研究终端运行 `tools/trace-dynamic-fonts.py`，然后创建 `private/dynamic-font-arm.txt`。探针确认控件已聚焦才提交三个样本。结束时创建 `private/dynamic-font-finish.txt`，脚本先关闭它附加的精确隔离游戏，再释放 Frida 会话。`scalar_probe.exe FONT_DIRECTORY UTF8_SOURCE OUTPUT_JSON` 可生成对应 16px CPU 参考；`--readback-only` 只读取当前聚焦字体的全部样本区域，不注入文本。

上述输入为受控 SDL 提交，不能代替物理输入法验收。此前候选窗、中文提交、整字退格、Shift 选择与替换已由用户确认；新字库更多字符的物理复验需要另记结果。

## 仍需完善

按需字形目前限于生成器明确预留的五种固定图集；普通系统/工坊小图集继续使用其已有字符。单页填满或实际字体缺字时仍返回既有占位。地图同样经过标量查找，但本次新增字形的游戏 GPU 证据仅覆盖 16px 单行 GUI；其他字号、地图、更多控件、长时间增长、失去设备和字体原位热重载仍需游戏验收。

这条桥接按单标量栅格化。阿拉伯文、印地文、双向混排、连字和完整组合序列仍必须接入整段 shaped run，并使测宽、绘制、光标和选区共享 cluster 结果。字库汉字覆盖完整与游戏完整 Unicode 支持是两项不同的验收；后续范围见 [路线图](roadmap.md)。
