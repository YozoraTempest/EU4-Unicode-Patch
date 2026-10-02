# 已安装汉化本地化迁移

本机工坊模组 2976470733 的 146 个 YML 文件全部能解码为 UTF-8，但其中的中文使用 EU4dll 的 0x10–0x13 转义协议。直接把文件交给新 UTF-8 绘制路径会显示错误字符，文件外层编码正确不能说明文本内容已经是普通 Unicode。

迁移工具只处理明确指定的 `eu4dll-escaped` 格式。协议先将 UTF-8 文件中的 CP1252 字符还原为载荷字节，再按四种转义标记恢复 UTF-16 单元；U+0101–U+09FF 的旧私用区重映射会还原，合法代理对会合并，截断或孤立代理项会报错。普通 Unicode、颜色、图标、变量、注释、BOM、换行及源文件结构均保持；不经过通用 YAML 重序列化。

协议来源为研究版本 EU4dll 的 `escape_tool.cpp` 与绘制汇编，提交见第三方声明。CP1252 的五个未定义字节按 Windows 既有行为保留对应 C1 值，仅用于还原转义载荷。

```powershell
python tools\migrate-localisation.py `
  'D:\SteamLibrary\steamapps\workshop\content\236850\2976470733\localisation' `
  private\migrated-localisation --format eu4dll-escaped
.\tools\prepare-test.ps1 -MigratedLocalisationDirectory private\migrated-localisation
.\tools\start-test.ps1
```

目标目录必须是独立的新目录，不覆盖原文件。全部输入通过验证后才创建输出，并生成 `unicode-migration.json`，记录每个原文件与输出文件的 SHA-256、转义数量和重映射数量。测试准备脚本核对输出文件哈希，然后将副本放入生成的私有测试模组；原工坊目录与正式游戏用户目录均不写入。

本机迁移共还原 2,781,965 个转义字符，私用区重映射数量为 0，转换文本包含 4,313 种字符。146 个原文件的 SHA-256 均与转换前一致，全部输出文件有效 UTF-8 且无残留旧标记。

该模式复用已有工坊字体的实际覆盖；系统字体模式当前只为小夹具生成图集，所以不能同时传 `-SystemFonts`。工坊字体中未收录的扩展汉字和更多语言仍可能显示占位，这不改变源文本的码点。

整包测试发现另一处地图标签顶点分配仍按原生单字节字体计数。UTF-8 绘制写入的顶点超过容量，破坏栈中的地形对象指针并触发访问冲突。已将该计数入口改为与绘制一致的码点及字形查找，运行跟踪同时检查实际顶点数不超过分配容量。完整游戏覆盖仍需长时间回归，不能只依据文本转换成功判定可发行。

修复后记录 186 个 Unicode 地图标签，实际顶点数均未超过分配容量，见 [顶点容量记录](evidence/migrated-map-budgets.json)。无 Frida 的原生交互证据包括 [菜单](evidence/migrated-menu.jpg)、[设置](evidence/migrated-settings.jpg)、[地图](evidence/migrated-map.jpg) 与 [战局](evidence/migrated-campaign.jpg)。截图和容量记录保留各自验收时的版本；不据此宣称其他地图模式和长期运行已通过。

返回小夹具模式时退出测试实例并运行 `prepare-test.ps1 -SystemFonts`。脚本只清理自己生成的测试模组本地化文件；迁移副本与原始汉化资源都保留。
