# 测试记录

测试平台：本机 Windows / Steam EU4 1.37.5.0 x64，非铁人。游戏验证使用独立用户目录与自有副本。

## v0.1.3 署名

本版仅调整署名与版本信息。九个 CTest、保护检查和两种字体安装方式的正常启动检查通过，初始化日志包含 `author=VulonLok`。[报告](evidence/player-attribution.json) · [主包日志](evidence/player-attribution-startup.log) · [可选字库日志](evidence/player-attribution-optional-startup.log)

字体实现未改动，设备像素检查沿用下列 v0.1.2 记录。

## v0.1.2 系统字体与可选字库

主包不附完整字体文件。无字库和装有可选字库两种安装均通过普通目录的游戏启动检查，记录了 18px 中文字形上传。检查使用系统模块枚举和日志，未注入观察工具；字体优先级为系统字体，原版字体定义保持原样。[报告](evidence/player-system-fonts.json) · [主包启动日志](evidence/player-system-fonts-startup.log) · [可选字库启动日志](evidence/player-optional-fonts-startup.log)

两种模式均通过 14、16、18、24、88px 五种字号的真实 D3D9 逐 alpha 字节检查。装有可选字库时，常用汉字像素仍与系统字体一致；系统缺少的 U+323B0 由可选字库补充。未装字库时，该缺字保留占位行为，常用汉字仍可生成。另检查工作线程仅生成 CPU 数据、旧区域保留、Reset 恢复、稳定指针和释放。[系统字体记录](evidence/player-system-atlas-device.log) · [可选字库记录](evidence/player-optional-atlas-device.log)

本版输入逻辑沿用已有实现，尚未完成整套人工输入复验。设备组件的像素检查与游戏内文字显示分别记录。

## v0.1.1 玩家版

正式 DLL 的 SHA-256：

```text
7c07a58218b8b58f9112ac47786dc7489de9f32da512d93ca25a3ec48375b7db
```

最终 DLL 和加载器已在普通目录启动，使用原版字体定义、未启用模组；旧 plugin64 文件保留但没有加载，MenuPatch 同时加载。最终加载器记录了 16/18px 中文字形上传；同一正式 DLL 的前一次运行还记录了 88px 地图扩展字形 U+20000 上传。前一次加载器的区别是线程通知设置，指纹单独保留。[报告](evidence/player-overlay.json) · [启动日志](evidence/player-overlay-startup.log) · [战局日志](evidence/player-overlay-campaign.log)

九个 CTest、研究路径保护、正式 EXE 保护、version API 参数转发和旧插件排除检查通过。玩家包的五种字号通过真实 D3D9 设备逐 alpha 字节比较；16px 另验证工作线程仅生成 CPU 数据、旧区域保留、Reset 恢复、实际纹理地址复用、稳定指针和拥有者释放。[设备记录](evidence/player-atlas-device.log)

设备组件检查与游戏内上传日志分别记录。最终玩家版的受控 SDL/GPU 全流程及整套人工输入复验尚未完成。

## 已有开发版记录

下列证据保留各自的 DLL 指纹，不表示全部案例已在最终玩家版重新执行。

| 路径 | 记录 |
| --- | --- |
| UI、本地化与格式 | [注册、绘制、颜色、图标及换行](evidence/runtime-trace.jsonl) |
| 地图与汉化迁移 | [146 文件迁移及地图顶点预算](evidence/migrated-map-budgets.json) |
| 按需汉字 | [三个受控 SDL 提交、14 个实际 GUI GPU 区域逐像素比较](evidence/native-dynamic-fonts.json) |
| 字体 | [Unicode 17 cmap 审计](evidence/open-font-coverage.json)、[64 次原生字体生命周期](evidence/native-font-lifetime.json) |
| 输入与选区 | [18 个编辑案例](evidence/native-editor.json)、[选区](evidence/native-selection.json)、[像素几何](evidence/native-editor-geometry.json)、[26 个 SDL 事件](evidence/native-sdl-input.json) |
| 剪贴板 | [受控原生粘贴](evidence/native-clipboard.json)、[复制/剪切往返](evidence/native-clipboard-roundtrip.json) |
| 中文输入法 | [候选窗用户确认](evidence/physical-ime-candidates.json)、[人工提交、退格、选择和替换](evidence/physical-editor-sequence.json) |
| 搜索与保存 | [外交国家名过滤](evidence/country-search.json)、[名称与路径往返](evidence/persistence-roundtrip.json)、[月度自动保存](evidence/autosave-roundtrip.json) |
| 复杂文字 | [独立 DirectWrite 布局](evidence/shaped-run-validation.json)，尚未接入游戏整段绘制 |

人工中文输入记录来自此前候选窗修补版，范围为当前 Windows 中文输入法、外交单行搜索框、1280×720、GUI scale 1。受控剪贴板案例不等于系统剪贴板及物理快捷键全流程验收。

## 待验证

其他模组、输入法与控件、缩放、多行和预编辑，长期战役及游戏设备恢复，其他保存入口、云存档、铁人和双端联机。功能开发计划见[后续任务](roadmap.md)。
