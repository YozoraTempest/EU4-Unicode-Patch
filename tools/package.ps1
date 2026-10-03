param(
    [ValidateSet('Release','Nightly')][string]$Channel='Release',
    [string]$BuildDate='',
    [string]$BuildDirectory='build'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
Assert-CleanCheckout
$info=Get-ReleaseInfo $Channel $BuildDate
$root=Get-BuildRoot $BuildDirectory
$validation=Get-Content -LiteralPath (Join-Path $root 'automated-validation.json') -Raw | ConvertFrom-Json
if ($validation.source_commit -ne $info.SourceCommit -or $validation.passed -ne $true -or
    @($validation.ctest).Count -ne 11 -or $validation.loader_guards -ne $true -or $validation.optional_fonts -ne $true -or
    $validation.patch_dll_sha256 -ne (Get-Sha256 (Join-Path $root 'eu4_unicode_patch.dll')) -or
    $validation.loader_sha256 -ne (Get-Sha256 (Join-Path $root 'VERSION.dll'))) {
    throw 'Automated validation is incomplete or belongs to another build.'
}
$game=Get-Content -LiteralPath (Join-Path $releaseProjectRoot 'tests/evidence/player-runtime-fonts.json') -Raw | ConvertFrom-Json
$gameVerified=$false
if ($game.dll_sha256 -eq $validation.patch_dll_sha256 -and $game.loader_sha256 -eq $validation.loader_sha256) {
    & (Join-Path $PSScriptRoot 'test-player-validation.ps1') -BuildDirectory $BuildDirectory | Out-Null
    $gameVerified=$true
}
& (Join-Path $PSScriptRoot 'stage-player.ps1') -Channel $Channel -BuildDate $info.BuildDate -BuildDirectory $BuildDirectory | Out-Null
& (Join-Path $PSScriptRoot 'stage-fonts.ps1') -Channel $Channel -BuildDate $info.BuildDate -BuildDirectory $BuildDirectory | Out-Null

function Get-PackageFiles([string]$Stage) {
    return @(Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path=[IO.Path]::GetRelativePath($Stage,$_.FullName).Replace('\','/')
            bytes=$_.Length
            sha256=(Get-Sha256 $_.FullName)
        }
    })
}
$playerRoot=Join-Path $root 'player-package'
$fontRoot=Join-Path $root 'player-fonts'
$playerManifest=[ordered]@{
    patch_version=$info.Version
    release_tag=$info.Tag
    channel=$info.Channel
    author='VulonLok'
    distribution='player drop-in'
    source_commit=$info.SourceCommit
    game_version='1.37.5.0 Inca Windows x64'
    game_exe_sha256=$game.exe_sha256
    patch_dll_sha256=$validation.patch_dll_sha256
    loader_sha256=$validation.loader_sha256
    font_preference='system first; optional font pack supplies missing glyphs'
    optional_font_package=$info.FontPackage
    automated_validation=$validation
    game_runtime_verified=$gameVerified
    files=(Get-PackageFiles $playerRoot)
}
$playerManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'player-manifest.json') -Encoding utf8NoBOM
[ordered]@{
    patch_version=$info.Version
    publisher='VulonLok'
    distribution='optional font pack'
    source_commit=$info.SourceCommit
    license='SIL-OFL-1.1'
    font_preference='system first; these files supply missing glyphs'
    fonts=(Get-Content -LiteralPath (Join-Path $releaseProjectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json).files
    files=(Get-PackageFiles $fontRoot)
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fontRoot 'plugins/eu4_unicode_patch/font-manifest.json') -Encoding utf8NoBOM

$dist=Join-Path $releaseProjectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$playerArchive=Join-Path $dist $info.PlayerPackage
$fontArchive=Join-Path $dist $info.FontPackage
Compress-Archive -Path (Join-Path $playerRoot '*') -DestinationPath $playerArchive -Force
Compress-Archive -Path (Join-Path $fontRoot '*') -DestinationPath $fontArchive -Force
[IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'),
    "$(Get-Sha256 $playerArchive)  $($info.PlayerPackage)`n$(Get-Sha256 $fontArchive)  $($info.FontPackage)`n",[Text.UTF8Encoding]::new($false))
$info | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'package-info.json') -Encoding utf8NoBOM
& (Join-Path $PSScriptRoot 'test-package.ps1') -BuildDirectory $BuildDirectory
Write-Output $playerArchive
Write-Output $fontArchive
