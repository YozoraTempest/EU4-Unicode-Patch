$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$patchVersion='0.1.2-experimental'
$packageName="EU4UnicodePatch-1.37.5-v$patchVersion-drop-in.zip"
$fontPackageName="EU4UnicodePatch-fonts-v$patchVersion.zip"
$validation=Get-Content -LiteralPath (Join-Path $projectRoot 'docs/evidence/player-system-fonts.json') -Raw | ConvertFrom-Json
foreach ($entry in @(
    @{Path='build/eu4_unicode_patch.dll';Hash=$validation.dll_sha256},
    @{Path='build/VERSION.dll';Hash=$validation.loader_sha256}
)) {
    if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $entry.Path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Hash) {
        throw 'The player DLL or loader differs from the validated build.'
    }
}
if ($validation.distribution -ne 'player overlay' -or $validation.loaded_modules.enabled -ne 1 -or
    $validation.loaded_modules.legacy_loaded -ne $false -or
    $validation.d3d9_component.exact_alpha -ne $true -or
    ($validation.d3d9_component.sizes -join ',') -ne '14,16,18,24,88' -or
    $validation.d3d9_component.system_only -ne $true -or
    $validation.d3d9_component.optional_font_pack -ne $true -or
    $validation.game_startup.optional_font_files -ne 0 -or
    $validation.optional_game_startup.optional_font_files -ne 3 -or
    $validation.font_preference -ne 'system first') {
    throw 'Player overlay validation is incomplete.'
}
foreach ($asset in $validation.assets) {
    $source=Join-Path $projectRoot "build/player-assets/$($asset.path)"
    if ((Get-Item -LiteralPath $source).Length -ne $asset.bytes -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) {
        throw 'Player font atlas differs from the validated assets.'
    }
}
$sourceCommit=& git -C $projectRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Package from a source checkout.' }
$changes=& git -C $projectRoot status --porcelain
if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Commit source changes before packaging.' }

& (Join-Path $PSScriptRoot 'stage-player.ps1') | Out-Null
$stageRoot=Join-Path $projectRoot 'build/player-package'
$packageFiles=@(Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path=[IO.Path]::GetRelativePath($stageRoot,$_.FullName).Replace('\','/')
        bytes=$_.Length
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$manifest=[ordered]@{
    patch_version=$patchVersion
    status='experimental'
    distribution='player drop-in'
    source_commit=$sourceCommit
    game_version='1.37.5.0 Inca Windows x64'
    game_exe_sha256=$validation.exe_sha256
    patch_dll_sha256=$validation.dll_sha256
    loader_sha256=$validation.loader_sha256
    unicode_input_default=$true
    font_preference='system first; optional font pack supplies missing glyphs'
    optional_font_package=$fontPackageName
    validation='Ordinary-directory startup with vanilla font definitions; five D3D9 sizes in system-only and optional-font modes, Reset and ownership verified'
    validation_limits='Latest player physical IME and complete controlled SDL/GPU sequence pending; complex shaping, multi-page atlases, Ironman and multiplayer unverified'
    files=$packageFiles
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stageRoot 'plugins/eu4_unicode_patch/manifest.json') -Encoding utf8NoBOM
$distRoot=Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$archive=Join-Path $distRoot $packageName
Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $archive -Force
$archiveHash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()

& (Join-Path $PSScriptRoot 'stage-fonts.ps1') | Out-Null
$fontStageRoot=Join-Path $projectRoot 'build/player-fonts'
$fontFiles=@(Get-ChildItem -LiteralPath $fontStageRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path=[IO.Path]::GetRelativePath($fontStageRoot,$_.FullName).Replace('\','/')
        bytes=$_.Length
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$fontManifest=[ordered]@{
    patch_version=$patchVersion
    distribution='optional font pack'
    source_commit=$sourceCommit
    license='SIL-OFL-1.1'
    font_preference='system first; these files supply missing glyphs'
    fonts=(Get-Content -LiteralPath (Join-Path $projectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json).files
    files=$fontFiles
}
$fontManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fontStageRoot 'plugins/eu4_unicode_patch/font-manifest.json') -Encoding utf8NoBOM
$fontArchive=Join-Path $distRoot $fontPackageName
Compress-Archive -Path (Join-Path $fontStageRoot '*') -DestinationPath $fontArchive -Force
$fontArchiveHash=(Get-FileHash -LiteralPath $fontArchive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $distRoot 'SHA256SUMS.txt'),
    "$archiveHash  $packageName`n$fontArchiveHash  $fontPackageName`n$($validation.dll_sha256)  plugins/eu4_unicode_patch.dll`n$($validation.loader_sha256)  VERSION.dll`n",[Text.UTF8Encoding]::new($false))
Write-Output $archive
Write-Output "SHA-256: $archiveHash"
Write-Output $fontArchive
Write-Output "SHA-256: $fontArchiveHash"
