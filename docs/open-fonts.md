# 字体

玩家包内置三个开源字体原文件，运行时使用进程私有的 DirectWrite 字体集合。

| 字体 | 版本 | 用途 |
| --- | --- | --- |
| [Source Han Sans SC Regular](https://github.com/adobe-fonts/source-han-sans/releases/tag/2.005R) | 2.005R | 中文界面，优先使用 |
| [Plangothic P1 / P2 Regular](https://github.com/Fitzgerald-Porthmouth-Koenigsegg/Plangothic_Project) | V2.9.5795 | 扩展汉字 |

字体固定版本与 SHA-256 见[清单](../fixtures/open-fonts.json)，SIL OFL 1.1 许可证随包提供。字体不安装到 Windows。

## 覆盖

三个字体的非零 cmap 映射并集包含 135,063 个标量，覆盖 Unicode 17 已分配的 102,998 个统一及兼容汉字。详见[审计报告](evidence/open-font-coverage.json)。字库覆盖不等于所有字符、IVS 和复杂文字都已通过游戏验收。

## 图集

玩家包预生成 14、16、18、24、88px 五张 2048×4096 图集，每张初始包含 192 个拉丁字符及省略号。图集仅从包内开源字体生成。原版文字字体路径在加载时转到补丁的独立目录；颜色与效果仍由原生字体定义控制。

未收录字符首次出现时生成字形并排队，在游戏原生纹理查找阶段上传。旧坐标和 UV 保留。共享纹理的字体共用一页，Reset 后从 CPU staging 恢复像素。

每页约 32 MiB GPU 内存；动态上传另占约 32 MiB CPU staging。五页全部加载并发生动态上传时约占 160 MiB GPU 和 160 MiB staging，另计字体和缓存。图集填满或实际缺字时仍可能显示占位符。

当前按单标量生成字形，尚未接入阿拉伯文等复杂文字所需的整段排版。多页图集、淘汰预算和活字体重载见[后续任务](roadmap.md)。

## 开发检查

```powershell
.\tools\prepare-player-assets.ps1
.\build\native_font_atlas_tests.exe build\player-assets private\open-fonts --player
```

独立字库审计需要 fontTools：

```powershell
python -m venv private\font-analysis
.\private\font-analysis\Scripts\python.exe -m pip install -r tools\requirements-font-audit.txt
.\private\font-analysis\Scripts\python.exe tools\audit-open-fonts.py
```

游戏补丁不依赖 Python。验收范围见[测试记录](validation.md)。
