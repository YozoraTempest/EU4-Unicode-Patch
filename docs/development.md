# 开发说明

补丁针对已校验的 EU4 1.37.5.0 x64。文本存储为标准 UTF-8，引擎字符串与光标位置使用字节偏移。

## 模块

| 模块 | 职责 |
| --- | --- |
| `unicode_text` / `unicode_services` | UTFCPP 编解码，ICU 字素、行边界与搜索键 |
| `unicode_editor` / `unicode_search` | 单行编辑、选区、字节预算与外交国家名过滤 |
| `unicode_layout` | DirectWrite 字体集合、布局与栅格化 |
| `glyph_registry` / `scalar_glyph` / `native_font_atlas` | 稀疏字形记录、按需图集和设备恢复 |
| `font_assets` / `font_atlas_assets` | 原版字体路径映射与运行时基础图集生成 |
| `plugin.cpp` / MASM | 指令检查、引擎挂钩与失败回滚 |
| `version_proxy` | Windows version API 转发及插件加载 |

`eu4_unicode_patch.dll` 用于玩家目录，输入默认开启；`eu4_unicode_probe.dll` 保留隔离目录和输入开关，供原生探针使用。

## 引擎入口

| 路径 | RVA |
| --- | --- |
| 字体加载 / 字体表析构 | `15953c0` / `1594360` |
| 纹理查找 | `16c3f10` |
| 单行按键 / 插入 | `15366c0` / `1536b80` |
| Windows IME 消息 / 光标矩形 | `1764940` / `17657c0` |

ASCII 保留 256 槽表，其他标量进入稳定的稀疏记录。字体路径通过游戏的字符串赋值函数替换，使用原生分配器；原版同名路径先查询引擎资源解析器（RVA `19fad70`）；`.fnt`、`.tga` 或 `.dds` 来自模组或压缩包时保留原路径。自定义路径保持原生加载，普通与放大 UI 路径分别处理。

## 字体

玩家版按需字形先用系统 DirectWrite 字体；缺字后才加载游戏目录中的可选字体文件。开发探针保留文件字体优先的验证方式。安装或移除字体包后需重启游戏。

基础图集在原生字体加载回调中从系统字体生成，共有 14、16、18、24、88px 五页，每页 2048×4096，初始包含 192 个基础字符。同一进程重复加载时复用，下一次启动重新生成，缓存位于 `gfx/fonts/eu4-unicode/cache/`；DirectWrite 初始化在 DLL 加载锁之外执行。可选字体的版本与 SHA-256 见[清单](../fixtures/open-fonts.json)，版权与许可证见[第三方说明](../THIRD_PARTY_NOTICES.md)。

模组位图字体保留已收录字形与度量；动态补字目前只用于补丁生成图集。

测宽阶段只生成 CPU 字形，纹理查找阶段上传，旧坐标和 UV 保留。共享纹理的字体共用一页，Reset 后从 CPU staging 恢复；补丁不持有 default-pool 纹理引用。

每页约占 32 MiB GPU 内存，动态上传另占约 32 MiB CPU staging。五页全部使用时分别约占 160 MiB，另计字体和缓存。图集满或字体缺字时仍可能显示占位符。

字库 cmap 覆盖审计使用 `tools/audit-open-fonts.py`，依赖见 `tools/requirements-font-audit.txt`。覆盖报告不代表所有字符和复杂文字都已通过游戏验收。

## 输入与保存

编辑通过 ICU 字素边界处理光标与选区。SDL 2.0.4 文本事件载荷最多 31 字节；每个提交保留完整 UTF-8，在原生队列中完成一次插入和通知。外交搜索只修改已观察的国家名过滤调用者。

保存路径修正代理对转换与比较，并保留已观察保存入口的 UTF-8 名称。完整复杂排版已有独立 DirectWrite 实现，尚未接入游戏的测宽、绘制和选区。

## 旧汉化迁移

旧汉化可能在 UTF-8 文件内使用双字节转义。明确指定旧协议，把转换结果写到新目录：

```powershell
python tools\migrate-localisation.py '旧模组的localisation目录' private\migrated-localisation --format eu4dll-escaped
```

工具保留 BOM、换行和文件结构，拒绝截断转义与孤立代理项，输出逐文件哈希报告。将转换结果用于单独的 UTF-8 模组副本；原目录保留。玩家安装不需要运行此工具。

## 游戏测试副本

准备开发探针：

```powershell
.\tools\prepare-runtime.ps1 -GameDirectory '游戏目录'
.\tools\prepare-test.ps1 -GameDirectory '游戏目录' -OpenFonts
.\tools\start-test.ps1 -ExperimentalInput
```

准备普通目录的玩家覆盖测试：

```powershell
.\tools\stage-player.ps1
.\tools\prepare-player-test.ps1 -GameDirectory '游戏目录' -SaveFile '测试存档.eu4'
```

玩家副本位于 `private/player-install/Europa Universalis IV`，独立用户目录为 `private/player-userdir`。脚本保留原版字体定义和原安装的旧插件，覆盖玩家包，不修改原安装。`-SaveFile` 可省略。EU4 的 `userdir.txt` 路径不要带尾部换行。

默认测试无可选字体的主包；加上 `-OptionalFonts` 可测试字体包安装后的行为。

`python tools/verify-mod-fonts.py` 在该副本中生成目录及压缩包测试模组，检查同名覆盖、自定义路径、纹理覆盖和放大字体的度量与实际 GPU 像素；脚本退出时恢复副本的模组配置。生成的系统字体纹理仅留在 `private/`。

## 原生探针

Frida 探针需要独立 Python 环境、Frida 和 psutil；剪贴板探针另需 pefile。MAP 必须与当前 DLL 一致。结束调试前先关闭专用游戏实例。

`tools/trace-*.py` 记录原生调用，`tools/verify-*.py` 检查结果。受控 SDL 注入、GPU 读回与人工输入分别记录，结果见[测试范围](validation.md)，原始记录位于 `tests/evidence/`。

动态 GPU 探针在外交搜索框聚焦后使用 `private/dynamic-font-arm.txt` 开始、`private/dynamic-font-finish.txt` 结束。`--player` 改为验证普通目录中的正式 DLL。

## 后续任务

- 图集：多页绘制、容量预算、缓存淘汰、字体重载及游戏设备恢复。
- 排版：将 DirectWrite 整段排版接入游戏测宽、绘制、光标和选区。
- 输入：更多输入法、控件、缩放、预编辑、多行、撤销及系统剪贴板。
- 保存：其他入口、输入产生的名称、自动保存周期及云存档。
- 游戏回归：更多模组、长期战役、铁人及双端联机、聊天与同步。
