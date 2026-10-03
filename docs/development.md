# 引擎适配与复验

补丁只适配[构建说明](build.md#版本保护)中的精确 EXE。文本存储为标准 UTF-8；引擎原生字符串和光标仍使用字节偏移。编码、Unicode 服务、排版、字形与引擎现场分层实现。

## 模块契约

| 模块 | 责任 |
| --- | --- |
| `unicode_text` | UTFCPP 编解码、32 位标量、完整字符截断和 SDL 文本载荷 |
| `unicode_services` | Windows ICU 字素/行边界和规范化搜索键 |
| `unicode_editor` | 单行编辑事务、字素删除、选区替换和字节预算 |
| `unicode_search` | 国家显示名匹配，缓存键与源文本分离 |
| `unicode_layout` | DirectWrite 系统/文件字体、整段布局、实际 glyph run 与栅格化 |
| `glyph_registry` | 原生 ASCII 表之外的稳定 Unicode 字形记录与拥有者寿命 |
| `scalar_glyph` / `native_font_atlas` | 按需标量位图、固定坐标、排队上传和设备恢复 |
| `plugin.cpp` / MASM | 精确指令检查、引擎入口、现场保存与启用失败回滚 |

### 字形与 GPU

ASCII 保留原生 256 槽表，U+0100–U+10FFFF 使用稀疏表，记录保持原生 16 字节布局。字体别名由共享 ASCII A 指针识别；拥有者析构先释放 Unicode 记录和图集绑定。加载阶段只查已加载记录，避免解析 FNT 时提前生成动态字形。

固定页的上传和预算见[字体说明](open-fonts.md)。旧 GPU 只读调查的 GUI 顶点 stride 为 28：FLOAT3 position、FLOAT2 UV、D3DCOLOR、第二组打包数据；FVF 为 0，使用顶点声明。后一组数据语义、着色器、裁剪和页批次仍需调查。原生顶点缓存会复用 UV，多页不能仅在字形查找时切换纹理。

### 输入与搜索

单行编辑以 ICU 字素边界处理光标和选区，提交保持有效 UTF-8。每个 SDL 文本事件穿过原生队列后一次插入，监听器只接收最终状态；禁止/非法/超预算提交保留应保留的选区。当前 SDL 2.0.4 的事件负载有 31 字节上限，原生粘贴走独立完整缓冲及释放契约。

光标矩形取正常绘制帧的位置；同坐标不重复提交，失焦停止更新，重新聚焦刷新。Windows IME 保留原生候选 UI 标志并设置 `CFS_EXCLUDE`，范围限于已观察的窗口消息与控件。物理确认和受控 API 结果见[测试记录](validation.md)。

国家名搜索只适配外交过滤调用者，不改变通用字符串查找的偏移返回契约。ICU NFKC casefold 处理规范/大小写/全角，同时保留既有重音与 Æ/ß 匹配；查询和候选原文不改写。每线程缓存限制 1024 条、1 MiB 文本载荷。

### 存档与路径

修正原生代理对转换/比较中的 `+0x10000`，继续使用原编码器容量和比较契约。保存名称及 Load Game 的已观察 CP1252 转写入口保留 UTF-8；路径检查仍由游戏执行。验收必须核对实际文件打开及对应字段，不能把 Continue 进入战局当成读取成功。

持久化夹具由事件和单位模板生成省份、首府、军队、舰队名称。读取验收先移除生成夹具，再检查压缩/非压缩文件和重启后的字段；否则会把重新生成名称误判为数据恢复。

## 旧汉化迁移

部分旧汉化文件外层是 UTF-8，内容仍使用 EU4dll 的 0x10–0x13 转义。迁移必须明确指定协议，不根据“能解码成 UTF-8”猜测。

```powershell
python tools\migrate-localisation.py '旧模组的localisation目录' private\migrated-localisation --format eu4dll-escaped
.\tools\prepare-test.ps1 -MigratedLocalisationDirectory private\migrated-localisation
.\tools\start-test.ps1
```

工具还原 CP1252 载荷、旧私用区映射和 UTF-16 代理对，拒绝截断/孤立代理项；保留 BOM、换行、标记、注释和结构，输出到新目录并生成逐文件哈希报告。准备脚本核对报告，复用原工坊字体；该整包模式当前不能与 `-SystemFonts` / `-OpenFonts` 合用。自定义安装路径时向准备脚本传入 `-GameDirectory`、`-FontDirectory`。

## 实际探针

在独立 Python 环境安装 Frida、psutil；剪贴板探针另需 pefile。Frida 代理与游戏高频文字路径有关，结束时先关闭其附加的专用游戏，再释放会话。普通交互启动无需代理。MAP 必须对应当前 DLL；不要热插入字体初始化挂钩。

| 验证目标 | 探针 | 独立检查 |
| --- | --- | --- |
| UTF-8 注册/绘制 | `trace-import.py` | `verify-trace.py` |
| 原生编辑与选择 | `trace-editor.py`、`trace-selection.py` | `verify-editor-trace.py`、`verify-selection.py` |
| 像素定位/截断 | `trace-editor-geometry.py` | `verify-editor-geometry.py` |
| 完整提交 | `trace-sdl-input.py` | `verify-sdl-input.py` |
| 粘贴、复制和剪切 | `trace-clipboard.py` | `verify-clipboard.py` |
| 光标输入矩形 | `trace-ime-rect.py` | `verify-ime-rect.py` |
| Windows 候选 UI | `observe-ime-candidates.py --contract` | `verify-ime-candidates.py` |
| 字体生命周期 | `trace-font-lifetime.py` | `verify-font-lifetime.py` |
| 国家名过滤 | `trace-search.py` | `verify-search-trace.py` |
| 路径/存档 | `trace-paths.py` | `verify-persistence-save.py` |
| 动态字体纹理 | `trace-dynamic-fonts.py` | `verify-dynamic-fonts.py` |

探针位于 `tools/`，原生值夹具位于对应 `*_cases.py`；参数见各脚本 `--help`。部分探针自行启动副本，部分附加已有实例；不要同时启动多个研究会话。

动态字体重建：用 `-OpenFonts` 准备，启用实验输入，进入战局并聚焦外交搜索框，运行 `trace-dynamic-fonts.py` 后创建 `private/dynamic-font-arm.txt`。它确认焦点后投递三个样本并读取实际绑定纹理。结束时创建 `private/dynamic-font-finish.txt`。`scalar_probe.exe FONT_DIRECTORY UTF8_SOURCE OUTPUT_JSON` 生成 16px CPU 参考；GPU 报告使用两份输入逐指标/像素比较。`--readback-only` 不注入文本，只读取已上传区域。

国家搜索使用 `-SystemFonts -SearchProbe`，进入法国战局后按脚本提示点击 Name 排序。持久化使用 `-SystemFonts -PersistenceProbe` 保存压缩样本；退出后以普通 `-SystemFonts` 重建，移除名称事件/模板，再读取并保存另一份非压缩样本。程序设置查询、模板生成名称和合成 SDL 提交各自不替代物理输入验收。

完整 shaped run 诊断通过 `unicode_layout_tests.exe` 导出；`compose-glyph-runs.py` 根据 mask 和基线组图，需 Pillow。诊断位图证明独立布局，不能作为游戏复杂排版证据。
