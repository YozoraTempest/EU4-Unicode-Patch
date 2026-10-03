param(
    [Parameter(Mandatory)][ValidateSet('Player','Fonts')][string]$Package,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if ($Package -eq 'Player') {
    $header="EU4 Unicode Patch`n`n项目代码采用 MIT 许可证，© 2026 VulonLok。以下各段适用于所标明的代码或字体资源。`n"
    $sections=@(
        @{Title='EU4 Unicode Patch — MIT';Source='LICENSE'},
        @{Title='MinHook — BSD 2-Clause';Source='vendor/minhook/LICENSE.txt'},
        @{Title='EU4dll — 游戏适配参考 / MIT';Source='third-party/EU4dll-LICENSE.txt'}
    )
} else {
    $header="EU4 Unicode Patch 可选字体包`n`n打包维护：VulonLok。字体版权归各字体作者所有。`n"
    $sections=@(
        @{Title='Source Han Sans SC Regular — SIL OFL 1.1';Source='third-party/SourceHanSans-OFL.txt'},
        @{Title='Plangothic P1 / P2 Regular — SIL OFL 1.1';Source='third-party/Plangothic-OFL.txt';Notice='Copyright (c) 2024 by Fitzgerald P. Köeingsegg. All rights reserved.'}
    )
}
$content=[Text.StringBuilder]::new($header)
foreach ($section in $sections) {
    $separator='='*72
    [void]$content.Append("`n$separator`n$($section.Title)`n$separator`n`n")
    if ($section.Notice) { [void]$content.Append("$($section.Notice)`n`n") }
    $text=[IO.File]::ReadAllText((Join-Path $projectRoot $section.Source),[Text.Encoding]::UTF8).Replace("`r`n","`n")
    [void]$content.Append($text)
    if (!$text.EndsWith("`n")) { [void]$content.Append("`n") }
}
[IO.File]::WriteAllText($OutputPath,$content.ToString(),[Text.UTF8Encoding]::new($false))
