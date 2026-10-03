# 构建与发布

需要 Windows x64、Visual Studio 2022 C++ Build Tools、MASM、Windows SDK、PowerShell 7、CMake 3.24+、Ninja 和 Git。

## 构建

```powershell
git clone --recurse-submodules https://github.com/YozoraTempest/EU4-Unicode-Patch.git EU4UnicodePatch
cd EU4UnicodePatch
.\tools\build.ps1
.\tools\test-guards.ps1
```

`build.ps1` 查找 MSVC 工具链，构建后运行十二个 CTest。重建前退出测试实例，避免 PDB 被占用。

| 输出 | 用途 |
| --- | --- |
| `build/VERSION.dll` | 玩家加载器 |
| `build/eu4_unicode_patch.dll` | 正式目录补丁，输入默认开启 |
| `build/eu4_unicode_probe.dll` | 保留隔离目录保护的开发探针 |
| `build/fontpack.exe` | 字体图集生成工具 |
| `build/*.map` | 对应 DLL 的原生符号 |

## 字体与设备检查

```powershell
.\tools\fetch-open-fonts.ps1
.\build\open_font_tests.exe private\open-fonts private\open-fonts.png
.\build\native_font_atlas_tests.exe build\runtime-font-assets private\no-font-pack --system-only
.\build\native_font_atlas_tests.exe build\runtime-font-assets private\open-fonts --player
.\build\native_font_pages_tests.exe build\runtime-font-assets
```

`font_cache` 测试生成本机系统字体图集，游戏首次加载字体时使用同一实现。系统字形及生成的缓存不进入发行包。设备检查分别验证无字库和可选字库模式，覆盖五种字号、系统优先、缺字补充、旧区域保留、Reset 与释放。

`native_font_pages_tests` 分配 2,000 个 88px 汉字，比较两页纹理的实际像素，并检查混合分页的索引绘制、引擎状态恢复、Reset 和释放。需要本机 D3D9 设备及支持这些汉字的系统字体。

测试记录位于 `tests/evidence/`。检查命令：

```powershell
Get-ChildItem tests\*.py | ForEach-Object {
    python $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "Evidence check failed: $($_.Name)" }
}
```

## 玩家包与发布

版本号由仓库根目录的 `VERSION` 管理。完整构建、自动检查和打包：

```powershell
.\tools\ci.ps1
.\tools\ci.ps1 -Channel Nightly
```

主包只有加载器、补丁 DLL、安装说明和合并许可证，直接覆盖进游戏目录。可选字体另包提供。自动检查记录、DLL 指纹与构建信息保存在 `build/`，不进入主包。

`main` 为正式分支，`develop` 为开发分支。提交和 PR 会运行 CI；Actions 中手动运行 **Nightly**，默认构建 `develop`，也可填写该分支上的提交 SHA。

工作流的 `run` 步骤使用 `sh`，Windows 构建和打包调用现有 PowerShell 7 脚本。

| 渠道 | 标签 | 主包 |
| --- | --- | --- |
| Nightly | `v版本-nightly-yyyyMMdd-短SHA` | `EU4UnicodePatch-1.37.5-nightly-yyyyMMdd-短SHA.zip` |
| 正式版 | `v版本` | `EU4UnicodePatch-1.37.5-v版本.zip` |

Nightly 日期按北京时间计算，标记为 Pre-release。正式发布前在 `develop` 更新 `VERSION` 和 `CHANGELOG.md`；将本仓库的 `develop` PR 合并到 `main` 后，自动构建该次合并提交并发布 Latest。其他来源的 PR 不发布正式版。

两种 Release 均附主包、可选字体包及 `SHA256SUMS.txt`。先上传到草稿，全部上传成功后公开；重跑可恢复草稿，已经公开的版本保留原标签和文件。正式版本号必须递增。

打包要求当前提交、DLL 和加载器与本次自动检查记录一致。本机游戏验收单独核对：

```powershell
.\tools\test-player-validation.ps1
```

该命令继续要求 DLL、加载器与本机验收记录严格匹配。CI 自动检查不替代游戏、GPU 和输入法验收，发布说明注明该构建是否匹配本机记录。

主包许可证为 `plugins/eu4_unicode_patch/LICENSE.txt`；字体包许可证为 `FONT_LICENSES.txt`。UTFCPP 的许可文本保留在源码中，纯二进制玩家包使用 Boost 许可证的分发例外。

## 版本校验

EU4 1.37.5.0 x64 EXE 的 SHA-256：

```text
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

正式 DLL 检查 EXE、已加载的旧插件及目标指令；开发探针另检查 `EU4UnicodePatch/private/runtime` 路径。适配其他游戏版本需要重新调查引擎入口。
