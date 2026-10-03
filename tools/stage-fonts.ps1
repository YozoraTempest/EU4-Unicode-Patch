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
$stageRoot=Join-Path $buildRoot 'player-fonts'
if (Test-Path -LiteralPath $stageRoot) {
    if ((Get-Item -LiteralPath $stageRoot).Attributes -band [IO.FileAttributes]::ReparsePoint -or
        (Resolve-Path -LiteralPath $stageRoot).Path -ne [IO.Path]::GetFullPath((Join-Path $buildRoot 'player-fonts'))) {
        throw 'Unexpected generated optional-font staging path.'
    }
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
$data=Join-Path $stageRoot 'plugins/eu4_unicode_patch'
New-Item -ItemType Directory -Path (Join-Path $data 'fonts') -Force | Out-Null
$fontManifest=Get-Content -LiteralPath (Join-Path $projectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json
foreach ($font in $fontManifest.files) {
    $source=Join-Path $projectRoot "private/open-fonts/$($font.name)"
    if ((Get-Item -LiteralPath $source).Length -ne $font.bytes -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $font.sha256) {
        throw 'Pinned open font verification failed.'
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $data 'fonts')
}
& (Join-Path $PSScriptRoot 'write-package-license.ps1') -Package Fonts -OutputPath (Join-Path $data 'FONT_LICENSES.txt')
$readme=[IO.File]::ReadAllText((Join-Path $projectRoot 'docs/optional-fonts.txt')).Replace('@PATCH_RELEASE@',$info.Tag)
[IO.File]::WriteAllText((Join-Path $stageRoot 'EU4UnicodePatch.FONTS.txt'),$readme,[Text.UTF8Encoding]::new($false))
$stageRoot
