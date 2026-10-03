# 构建与发布

需要 Windows x64、Visual Studio 2022 C++ Build Tools、MASM、Windows SDK、PowerShell 7、CMake 3.24+、Ninja 和 Git。

## 构建

```powershell
git clone --recurse-submodules https://github.com/YozoraTempest/EU4-Unicode-Patch.git EU4UnicodePatch
cd EU4UnicodePatch
.\tools\build.ps1
.\tools\test-guards.ps1
```

`build.ps1` 查找 MSVC 工具链，构建后运行九个 CTest。重建前退出测试实例，避免 PDB 被占用。

| 输出 | 用途 |
| --- | --- |
| `build/VERSION.dll` | 玩家加载器 |
| `build/eu4_unicode_patch.dll` | 正式目录补丁，输入默认开启 |
| `build/eu4_unicode_probe.dll` | 保留隔离目录保护的开发探针 |
| `build/fontpack.exe` | 字体图集生成工具 |
| `build/*.map` | 对应 DLL 的原生符号 |

## 字体与设备检查

```powershell
.\tools\prepare-player-assets.ps1
.\build\open_font_tests.exe private\open-fonts private\open-fonts.png
.\build\native_font_atlas_tests.exe build\player-assets private\no-font-pack --system-only
.\build\native_font_atlas_tests.exe build\player-assets private\open-fonts --player
```

基础图集只从固定开源字体生成。运行时系统字体由玩家本机提供。设备检查分别验证无字库和可选字库模式，覆盖五种字号、系统优先、缺字补充、旧区域保留、Reset 与释放。

测试记录位于 `tests/evidence/`。检查命令：

```powershell
Get-ChildItem tests\*.py | ForEach-Object {
    python $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "Evidence check failed: $($_.Name)" }
}
```

## 玩家包

```powershell
.\tools\stage-player.ps1
.\tools\stage-fonts.ps1
.\tools\package.ps1
```

生成主包 `dist/EU4UnicodePatch-1.37.5-v0.1.4-experimental-drop-in.zip`、可选字体包 `dist/EU4UnicodePatch-fonts-v0.1.4-experimental.zip` 和 `dist/SHA256SUMS.txt`。两个 ZIP 均直接覆盖进游戏目录，源码、测试模组和游戏资源不进入玩家包。

打包脚本核对正式 DLL、加载器、已验收图集及固定字体的 SHA-256。主包的打包记录写入 `build/player-manifest.json`；字体包保留 `font-manifest.json`。发布说明见 [GitHub Release](https://github.com/YozoraTempest/EU4-Unicode-Patch/releases/tag/v0.1.4-experimental)。

主包包含 14 个文件，许可证合并为 `plugins/eu4_unicode_patch/LICENSE.txt`。字体包的许可证合并为 `FONT_LICENSES.txt`，两包互不覆盖。UTFCPP 的许可文本保留在源码中，纯二进制玩家包使用 Boost 许可证的分发例外。

## 版本校验

EU4 1.37.5.0 x64 EXE 的 SHA-256：

```text
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

正式 DLL 检查 EXE、已加载的旧插件、资源及目标指令；开发探针另检查 `EU4UnicodePatch/private/runtime` 路径。适配其他游戏版本需要重新调查引擎入口。
