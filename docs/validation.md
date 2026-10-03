# 测试记录

平台为本机 Windows / Steam EU4 1.37.5.0 x64，游戏测试使用独立副本、测试模组和用户目录。下表汇总各功能完成时的记录；各证据文件保留其实际 DLL 指纹，不表示所有历史案例已在当前 DLL 上重新执行。

## 当前发布 DLL

v0.1.0-experimental 的 DLL SHA-256：

```text
d71bd5fba5d87b40bd7d0506aa2c1f1a2ae030145df3d259dff3ae65f1042f37
```

当前 DLL 已通过七个 CTest、两个错误宿主保护检查，以及真实 EU4 外交搜索框的三个受控 SDL 提交：“中华人民共和国”“孔雀翡翠”“𠮷𰀀𲎰”。每例核对完整 UTF-8、两次队列复制、正确光标、一次插入和一次通知。

实际绑定的 GPU 纹理中，14 个字符的尺寸、偏移、advance 和每个 alpha 字节与独立参考相同，均未取到省略号；11 个为动态新增字形。U+20BB7 来自思源黑体，U+30000、U+323B0 来自遍黑体 P2。[原始记录](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-dynamic-fonts.jsonl) · [GPU 报告](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-dynamic-fonts.json) · [CPU 参考](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/dynamic-font-cpu.json)

九个文件字体回退样本及 retained face 寿命通过。真实 D3D9 设备/模拟原生拥有者测试通过工作线程仅生成 CPU 数据、旧区域保留、扩展字符、Reset 恢复、实际纹理地址复用、稳定指针和拥有者释放。它证明设备契约，不替代游戏窗口/设备恢复的长期回归。

## 功能证据

| 路径 | 已有验证 | 证据 |
| --- | --- | --- |
| 本地化、UI、格式 | UTF-8 注册/绘制、按钮、颜色、资源图标、续字节格式碰撞和中文换行 | [运行记录](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/runtime-trace.jsonl) |
| 整包汉化与地图 | 146 文件迁移，2,781,965 个转义还原；186 个 Unicode 地图标签顶点未超容量，原文件哈希不变 | [地图预算](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/migrated-map-budgets.json) |
| 字体覆盖 | Unicode 17 已分配的 102,998 个统一/兼容汉字均有非零 cmap 映射；只证明字体覆盖 | [字库审计](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/open-font-coverage.json) |
| 原生字体生命周期 | 五种字号 64 次构造/加载/析构，地址复用后旧别名失效，活字形保留 | [生命周期报告](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-font-lifetime.json) |
| 独立复杂布局 | 实际字体、RTL、cluster、偏移、基线相位及字体寿命；尚未接入游戏整段绘制 | [布局记录](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/shaped-run-validation.json) |
| 原生单行编辑 | 18 个无选区案例；18 组选区、27 个状态；11 组、727 个像素位置 | [编辑](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-editor.json)、[选区](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-selection.json)、[几何](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-editor-geometry.json) |
| SDL 完整提交 | 26 个合成事件，包含非法/全禁用拒绝、选区替换和字节预算，接受时只插入/通知一次 | [SDL 报告](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-sdl-input.json) |
| 剪贴板受控来源 | 16 个原生粘贴案例；16 个复制/剪切/再粘贴往返；实际分配/释放核对 | [粘贴](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-clipboard.json)、[往返](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-clipboard-roundtrip.json) |
| IME API | 九个 SDL 光标矩形案例及失焦/重新聚焦；七个 Windows 消息契约、十七次定位读回 | [矩形](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-ime-rect.json)、[候选窗](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-ime-candidate-contract.json) |
| 物理中文输入 | 用户确认候选可见且位置正常；提交、整字退格、Shift 选择与替换，九个状态最终为“中字” | [用户确认](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/physical-ime-candidates.json)、[实际编辑](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/physical-editor-sequence.json) |
| 国家显示名搜索 | 12 组查询，包含规范等价、全角、重音、ß、希腊文、西里尔文及扩展汉字；保留原文 | [搜索记录](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/country-search.json) |
| 存档、路径和自动保存 | 代理对转换/比较、四类名称压缩→非压缩往返与重启读取；本地月度保存、轮换、两种格式恢复 | [持久化](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/persistence-roundtrip.json)、[路径比较](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/native-path-comparison.json)、[自动保存](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/autosave-roundtrip.json) |

物理输入记录来自此前候选窗修补 DLL `d02e0ce…`，范围为当前 Windows 中文输入法、外交单行搜索框、1280×720、GUI scale 1。当前新字库 DLL 的三组输入为受控 SDL 提交，更多汉字的物理复验尚未补充。剪贴板的受控来源也不等于系统剪贴板或物理快捷键完整链路通过。

旧 `save-roundtrip.json` 只保留写入记录；`fd08cff` 曾把默认世界初始化误认为非 BMP 读取成功，该结论已撤回。现有读取验收要求原生文件打开成功、移除名称生成夹具后仍恢复正确字段。

原始 JSON/JSONL 由证据测试使用，保留在源码仓库；发布包提供 DLL 和简明文档，证据链接指向仓库。独立 CPU 诊断图片、API 返回成功和游戏画面各有范围，不能相互替代。

## 未完成范围

多页图集与淘汰预算、活字体原位重载、游戏长期设备恢复、完整 shaped run 的 GPU/测宽/选区集成；其他控件/字号/地图动态字形、输入法预编辑、多行、系统剪贴板和物理鼠标选区；其他保存入口、云存档、铁人及联机。推进顺序见[后续任务](roadmap.md)。
