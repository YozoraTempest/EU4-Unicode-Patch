# 测试范围

测试平台：Windows / Steam EU4 1.37.5.0 x64，非铁人。游戏测试使用自有副本与独立用户目录；记录保留对应的 DLL 指纹。

## 玩家包

v0.1.4 通过九个 CTest、加载保护及两种字体安装方式的普通目录启动检查，记录了 18px 中文字形上传。主包包含 14 个文件，测试副本未保留旧包的清单和许可证目录。旧双字节插件未加载，MenuPatch 同时加载。[报告](../tests/evidence/player-package-cleanup.json) · [主包日志](../tests/evidence/player-package-cleanup-startup.log) · [可选字库日志](../tests/evidence/player-package-cleanup-optional-startup.log)

字体实现未改动，设备检查沿用 v0.1.2：14、16、18、24、88px 的实际 D3D9 alpha 字节比较，以及工作线程仅生成 CPU 数据、旧区域保留、Reset、稳定指针和释放。安装可选字库后，常用汉字仍使用系统字体，系统缺少的 U+323B0 由字库补充。[报告](../tests/evidence/player-system-fonts.json) · [系统字体设备日志](../tests/evidence/player-system-atlas-device.log) · [可选字库设备日志](../tests/evidence/player-optional-atlas-device.log)

## 开发版记录

以下记录来自此前开发构建，不表示已在当前玩家 DLL 完成全流程复验。

| 范围 | 记录 |
| --- | --- |
| 中文输入法 | 用户确认[候选窗](../tests/evidence/physical-ime-candidates.json)和[提交、退格、选区替换](../tests/evidence/physical-editor-sequence.json)正常；限外交单行搜索框、1280×720、GUI scale 1 |
| 输入与绘制 | [编辑](../tests/evidence/native-editor.json)、[选区](../tests/evidence/native-selection.json)、[SDL 事件](../tests/evidence/native-sdl-input.json)、[动态字形 GPU 比较](../tests/evidence/native-dynamic-fonts.json) |
| 剪贴板 | [受控粘贴](../tests/evidence/native-clipboard.json)与[复制、剪切往返](../tests/evidence/native-clipboard-roundtrip.json)，尚未完成系统快捷键全流程验收 |
| 搜索与保存 | [外交国家名](../tests/evidence/country-search.json)、[名称与路径往返](../tests/evidence/persistence-roundtrip.json)、[月度自动保存](../tests/evidence/autosave-roundtrip.json) |
| 字体与布局 | [字库 cmap 覆盖](../tests/evidence/open-font-coverage.json)与[独立 DirectWrite 布局](../tests/evidence/shaped-run-validation.json)，整段排版尚未接入游戏 |

## 尚未验证

当前玩家版的整套人工输入及受控 SDL/GPU 流程，更多输入法、控件、缩放、多行与预编辑，长期战役、游戏设备恢复、其他保存入口、云存档、铁人和双端联机。开发任务见[开发说明](development.md#后续任务)。
