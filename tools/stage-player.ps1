param(
    [ValidateSet('Release','Nightly')][string]$Channel='Release',
    [string]$BuildDate='',
    [string]$BuildDirectory='build'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
$info=Get-ReleaseInfo $Channel $BuildDate
$buildRoot=Get-BuildRoot $BuildDirectory
$projectRoot=Split-Path $PSScriptRoot -Parent
$stageRoot=Join-Path $buildRoot 'player-package'
$dll=Join-Path $buildRoot 'eu4_unicode_patch.dll'
foreach ($file in @($dll,(Join-Path $buildRoot 'VERSION.dll'))) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Build the player DLL and loader first.' }
}
if (Test-Path -LiteralPath $stageRoot) {
    if ((Get-Item -LiteralPath $stageRoot).Attributes -band [IO.FileAttributes]::ReparsePoint -or
        (Resolve-Path -LiteralPath $stageRoot).Path -ne [IO.Path]::GetFullPath((Join-Path $buildRoot 'player-package'))) {
        throw 'Unexpected generated player staging path.'
    }
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
$plugins=Join-Path $stageRoot 'plugins'
$data=Join-Path $plugins 'eu4_unicode_patch'
New-Item -ItemType Directory -Path $plugins,$data -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'VERSION.dll') -Destination $stageRoot
Copy-Item -LiteralPath $dll -Destination $plugins
$readme=[IO.File]::ReadAllText((Join-Path $projectRoot 'docs/direct-install.txt')).Replace('@PATCH_RELEASE@',$info.Tag).Replace('@FONT_PACKAGE@',$info.FontPackage)
[IO.File]::WriteAllText((Join-Path $stageRoot 'EU4UnicodePatch.README.txt'),$readme,[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'write-package-license.ps1') -Package Player -OutputPath (Join-Path $data 'LICENSE.txt')
$stageRoot
