# 第三方依赖与研究来源

- UTFCPP v4.0.8，提交 `f9319195dfddf369f68f18e7c0039b3f351797fd`，Boost Software License 1.0。项目：https://github.com/nemtrif/utfcpp 。许可证：`vendor/utfcpp/LICENSE`。
- MinHook v1.3.4，提交 `c3fcafdc10146beb5919319d0683e44e3c30d537`，BSD 2-Clause。项目：https://github.com/TsudaKageyu/minhook 。许可证：`vendor/minhook/LICENSE.txt`。
- Source Han Sans SC Regular 2.005R，Adobe，© 2014–2025，保留字体名 `Source`。项目：https://github.com/adobe-fonts/source-han-sans 。字体原文件采用 SIL Open Font License 1.1；许可证：`third-party/SourceHanSans-OFL.txt`。
- Plangothic P1 / P2 Regular，发行版 V2.9.5795，字体版权字段为 `Copyright (c) 2024 by Fitzgerald P. Köeingsegg. All rights reserved.`。项目：https://github.com/Fitzgerald-Porthmouth-Koenigsegg/Plangothic_Project 。字体采用 SIL Open Font License 1.1；许可证：`third-party/Plangothic-OFL.txt`。保留字体名遵循上游说明中的 `Plangothic` / `遍黑`。分发原文件，不修改字形或名称。
- EU4dll，参考提交 `365350dadbd868a23b014a739aad48bd3e965332`。项目：https://github.com/matanki-saito/EU4dll 。字体与字符循环适配参考该项目；玩家加载器沿用其 version API 转发和 `plugins` 目录加载约定，使用 x64 汇编转发，不执行旧自动更新脚本。本项目使用标准 UTF-8，不使用双字节转义编码。MIT 许可证保留在 `third-party/EU4dll-LICENSE.txt`。

玩家包包含本项目构建的 `VERSION.dll`、Unicode 补丁、固定开源字体原文件及由它们生成的 FNT/DDS 图集。不包含游戏资源、DLC、工坊字体或系统字体。

Unicode 边界、字素与搜索键使用 Windows 自带 ICU；字体回退与复杂文字布局使用系统 DirectWrite，诊断位图使用 Direct2D / WIC。这些系统组件及系统字体不随本项目分发。相关 API 由 Windows SDK 提供。

开源字体版本、来源与 SHA-256 固定在 `fixtures/open-fonts.json`。发行包的三个原文件不修改名称或内容；生成图集的 face 名称为 `EU4 Unicode open fonts`，不使用上游保留字体名。字体及生成图集遵循相应 OFL，项目代码的 MIT 许可证不改变字体授权。包内授权文件位于 `plugins/eu4_unicode_patch/licenses/`。

字库审计使用 fontTools 4.66.1（MIT，https://github.com/fonttools/fonttools），由独立 Python 环境安装；补丁运行不依赖 Python / fontTools。Unicode 17 数据仅下载到私有审计目录，报告引用 https://www.unicode.org/Public/17.0.0/ucd/ ，不随包分发数据文件。

官方文档：[Windows ICU](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-)、[ICU 边界分析](https://unicode-org.github.io/icu/userguide/boundaryanalysis/)、[DirectWrite](https://learn.microsoft.com/en-us/windows/win32/directwrite/introducing-directwrite)。
