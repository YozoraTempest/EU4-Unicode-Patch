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
.\build\native_font_atlas_tests.exe build\player-assets private\open-fonts --player
```

生成图集时只允许固定开源字体提供字形，发现系统字体或缺字即失败。设备检查使用真实 D3D9，覆盖五种字号、扩展汉字、旧区域保留、Reset、纹理地址复用与释放。

历史证据检查：

```powershell
Get-ChildItem tests\*.py | ForEach-Object {
    python $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "Evidence check failed: $($_.Name)" }
}
```

## 玩家包

```powershell
.\tools\stage-player.ps1
.\tools\package.ps1
```

生成 `dist/EU4UnicodePatch-1.37.5-v0.1.1-experimental-drop-in.zip` 和 `dist/SHA256SUMS.txt`。ZIP 顶层为 `VERSION.dll`、`plugins/`、`gfx/` 和安装说明。源码、准备脚本、测试模组和游戏资源不进入玩家包。

打包脚本核对正式 DLL、加载器、已验收图集及固定字体的 SHA-256。包内 `plugins/eu4_unicode_patch/manifest.json` 记录源提交与文件校验值。发布说明见 [v0.1.1](releases/v0.1.1-experimental.md)。

## 版本校验

EU4 1.37.5.0 x64 EXE 的 SHA-256：

```text
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

正式 DLL 检查 EXE、已加载的旧插件、资源及目标指令；开发探针另检查 `EU4UnicodePatch/private/runtime` 路径。适配其他游戏版本需要重新调查引擎入口。
