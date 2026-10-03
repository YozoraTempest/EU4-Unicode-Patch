# 第三方依赖与研究来源

- UTFCPP v4.0.8，提交 `f9319195dfddf369f68f18e7c0039b3f351797fd`，Boost Software License 1.0。项目：https://github.com/nemtrif/utfcpp 。许可证：`vendor/utfcpp/LICENSE`。
- MinHook v1.3.4，提交 `c3fcafdc10146beb5919319d0683e44e3c30d537`，BSD 2-Clause。项目：https://github.com/TsudaKageyu/minhook 。许可证：`vendor/minhook/LICENSE.txt`。
- Source Han Sans SC Regular 2.005R，Adobe，© 2014–2025，保留字体名 `Source`。项目：https://github.com/adobe-fonts/source-han-sans 。字体原文件采用 SIL Open Font License 1.1；许可证：`third-party/SourceHanSans-OFL.txt`。
- Plangothic P1 / P2 Regular，发行版 V2.9.5795，字体版权字段为 `Copyright (c) 2024 by Fitzgerald P. Köeingsegg. All rights reserved.`。项目：https://github.com/Fitzgerald-Porthmouth-Koenigsegg/Plangothic_Project 。字体采用 SIL Open Font License 1.1；许可证：`third-party/Plangothic-OFL.txt`。保留字体名遵循上游说明中的 `Plangothic` / `遍黑`。分发原文件，不修改字形或名称。
- EU4dll，研究提交 `365350dadbd868a23b014a739aad48bd3e965332`。项目：https://github.com/matanki-saito/EU4dll 。该项目的 1.37 字体扩容、字符循环位置和原始指令为本机静态调查提供参考；本项目重新实现标准 UTF-8 解码与现场保存，不使用其转义文本编码。MIT 许可证保留在 `third-party/EU4dll-LICENSE.txt`。

本机 Steam 游戏资源、工坊中文字体和已安装的 version.dll 加载器仅用于私有运行夹具，不进入分发包。游戏截图记录本机测试结果。

Unicode 边界、字素与搜索键使用 Windows 自带 ICU；字体回退与复杂文字布局使用系统 DirectWrite，诊断位图使用 Direct2D / WIC。这些系统组件及系统字体不随本项目分发。相关 API 由 Windows SDK 提供。

开源字体版本、来源与 SHA-256 固定在 `fixtures/open-fonts.json`；`tools/fetch-open-fonts.ps1` 获取到私有目录，发行包的 `fonts/` 只包含三个经校验的开源原文件及上述许可证。游戏字体图集和系统字体仍不进入包。字体及其字形诊断位图遵循相应的 OFL，项目代码的 MIT 许可证不改变字体授权。

字库审计使用 fontTools 4.66.1（MIT，https://github.com/fonttools/fonttools），由独立 Python 环境安装；补丁运行不依赖 Python / fontTools。Unicode 17 数据仅下载到私有审计目录，报告引用 https://www.unicode.org/Public/17.0.0/ucd/ ，不随包分发数据文件。

官方文档：[Windows ICU](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-)、[ICU 边界分析](https://unicode-org.github.io/icu/userguide/boundaryanalysis/)、[DirectWrite](https://learn.microsoft.com/en-us/windows/win32/directwrite/introducing-directwrite)。
