$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$stageRoot=Join-Path $projectRoot 'build/player-package'
$dll=Join-Path $projectRoot 'build/eu4_unicode_patch.dll'
foreach ($file in @($dll,(Join-Path $projectRoot 'build/VERSION.dll'))) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Build the player DLL and loader first.' }
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
& (Join-Path $PSScriptRoot 'write-package-license.ps1') -Package Player -OutputPath (Join-Path $data 'LICENSE.txt')
$stageRoot
