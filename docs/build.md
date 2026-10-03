# 构建与发布

## 环境

- Windows x64，Windows 10 1903 或更新版本。
- Visual Studio 2022 Build Tools，MSVC x64、MASM 和 Windows SDK。
- PowerShell 7、CMake 3.24+、Ninja、Git。
- Python 3 用于迁移和证据校验；补丁运行不依赖 Python。

克隆源码时保留所需目录名并初始化依赖：

```powershell
git clone --recurse-submodules https://github.com/YozoraTempest/EU4-Unicode-Patch.git EU4UnicodePatch
cd EU4UnicodePatch
.\tools\build.ps1
.\tools\test-guards.ps1
```

构建脚本通过 `vswhere` 查找工具链，并运行七个 CTest。`-Fresh` 重新配置，`-BuildDirectory` 指定输出目录。重建前退出测试游戏，避免其调试组件占用 PDB。GitHub 自动生成的 Source code 压缩包不包含子模块内容；完整构建使用上述克隆方式。

| 输出 | 用途 |
| --- | --- |
| `build/eu4_unicode_probe.dll` | 带隔离目录保护的实验补丁 |
| `build/fontpack.exe` | 生成测试 FNT/DDS 图集 |
| `build/eu4_unicode_probe.map` | 原生探针的符号定位 |
| `build/open_font_tests.exe` | 文件字体回退、栅格与字体寿命检查 |
| `build/native_font_atlas_tests.exe` | 真实 D3D9 设备的上传、Reset 和释放检查 |

## 检查

核心测试覆盖 UTF-8、ICU 边界/搜索、DirectWrite 布局、稀疏字形表、编辑事务和 IME 消息。保护检查验证无关目录和错误 EXE 被拒绝。

已提交的原始证据可独立复验；下列命令失败时停止后续检查：

```powershell
Get-ChildItem tests\*.py | ForEach-Object {
    python $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "Evidence check failed: $($_.Name)" }
}
python tools\verify-physical-editor.py docs\evidence\physical-editor-sequence.json
```

文件字体和 GPU 设备测试需要先准备开源字体夹具：

```powershell
.\tools\prepare-test.ps1 -OpenFonts
.\build\open_font_tests.exe private\open-fonts private\open-font-fallback.png
.\build\native_font_atlas_tests.exe private\test-mod private\open-fonts
```

真实游戏探针另需 Frida、psutil，剪贴板探针还需 pefile；诊断图片组图需 Pillow。使用本项目独立 Python 环境，运行方法见[引擎适配与复验](development.md)。[CI 模板](ci/windows-build.yml)尚未启用远端运行。

## 打包

```powershell
.\tools\fetch-open-fonts.ps1
.\tools\package.ps1
```

生成 `dist/EU4UnicodePatch-1.37.5-v0.1.0-experimental-isolated.zip` 和 `dist/SHA256SUMS.txt`。包内顶层目录固定为 `EU4UnicodePatch`，只包含运行所需 DLL、字体生成器、字体/许可证、准备脚本、夹具和简明文档；源码、研究探针、原始证据在仓库保留。

打包脚本检查字体固定哈希、DLL 与最近实际 GPU 验收指纹一致，并生成包内 `manifest.json`。发布时使用对应提交创建 `v0.1.0-experimental` 标签，标记为 prerelease，附加上述 ZIP 和校验文件；正文来自 [release 说明](releases/v0.1.0-experimental.md)。

## 版本保护

仅支持以下 EU4 1.37.5.0 x64 EXE：

```text
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

DLL 还检查隔离目录、旧 `plugin64.dll` 冲突和待修改的原始指令。支持其他版本需要重新适配引擎入口；仅更换哈希不能完成适配。
