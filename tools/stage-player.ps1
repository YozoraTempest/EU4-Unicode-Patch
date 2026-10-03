$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$stageRoot=Join-Path $projectRoot 'build/player-package'
$dll=Join-Path $projectRoot 'build/eu4_unicode_patch.dll'
foreach ($file in @($dll,(Join-Path $projectRoot 'build/VERSION.dll'))) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Build the player DLL and loader first.' }
}
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'build/player-assets/gfx/fonts/eu4-unicode/zh-hans-map.dds'))) {
    throw 'Run tools/prepare-player-assets.ps1 first.'
}
if (Test-Path -LiteralPath $stageRoot) {
    if ((Resolve-Path -LiteralPath $stageRoot).Path -ne [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/player-package'))) {
        throw 'Unexpected generated player staging path.'
    }
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
$plugins=Join-Path $stageRoot 'plugins'
$data=Join-Path $plugins 'eu4_unicode_patch'
New-Item -ItemType Directory -Path $plugins,$data -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build/VERSION.dll') -Destination $stageRoot
Copy-Item -LiteralPath $dll -Destination $plugins
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/direct-install.txt') -Destination (Join-Path $stageRoot 'EU4UnicodePatch.README.txt')
$atlasTarget=Join-Path $stageRoot 'gfx/fonts/eu4-unicode'
New-Item -ItemType Directory -Path $atlasTarget -Force | Out-Null
foreach ($name in @('zh-hans-14','zh-hans-16','zh-hans-18','zh-hans-24','zh-hans-map')) {
    foreach ($extension in @('fnt','dds')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot "build/player-assets/gfx/fonts/eu4-unicode/$name.$extension") -Destination $atlasTarget
    }
}
& (Join-Path $PSScriptRoot 'write-package-license.ps1') -Package Player -OutputPath (Join-Path $data 'LICENSE.txt')
$stageRoot
