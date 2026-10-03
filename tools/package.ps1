$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$patchVersion='0.1.6-experimental'
$packageName="EU4UnicodePatch-1.37.5-v$patchVersion-drop-in.zip"
$fontPackageName="EU4UnicodePatch-fonts-v$patchVersion.zip"
$validation=Get-Content -LiteralPath (Join-Path $projectRoot 'tests/evidence/player-runtime-fonts.json') -Raw | ConvertFrom-Json
$pages=Get-Content -LiteralPath (Join-Path $projectRoot 'tests/evidence/player-map-pages.json') -Raw | ConvertFrom-Json
if ($pages.dll_sha256 -ne $validation.dll_sha256 -or $pages.exe_sha256 -ne $validation.exe_sha256 -or
    $pages.gpu.glyphs -lt 2000 -or $pages.gpu.pages -lt 2 -or
    $pages.gpu.exact_alpha -ne $true -or $pages.gpu.mixed_indexed_draw -ne $true -or
    $pages.gpu.engine_state_restored -ne $true -or $pages.gpu.reset_restored -ne $true -or
    $pages.mt.paged_draw -ne $true -or $pages.mt.capacity_errors -ne 0 -or $pages.mt.draw_errors -ne 0) {
    throw 'Map font pagination validation is incomplete or belongs to a different build.'
}
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
    $validation.runtime_generation.system_font_pixels -ne $true -or
    $validation.runtime_generation.bundled_font_assets -ne 0 -or
    $validation.mod_fonts.same_path_override -ne $true -or
    $validation.mod_fonts.custom_path -ne $true -or
    $validation.mod_fonts.archive_override -ne $true -or
    $validation.mod_fonts.texture_only_override -ne $true -or
    $validation.mod_fonts.enlarged_ui -ne $true -or
    $validation.mod_fonts.exact_metrics_and_pixels -ne $true -or
    $validation.font_preference -ne 'system first') {
    throw 'Player overlay validation is incomplete.'
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
    author='VulonLok'
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
    validation='Runtime system fonts; native mod font overrides; five D3D9 sizes; 2000 CJK glyphs, mixed map pages, Reset and ownership; MT campaign startup'
    validation_limits='UI pagination, latest player physical IME, complex shaping, in-game device recovery, long campaigns, Ironman and multiplayer unverified'
    files=$packageFiles
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $projectRoot 'build/player-manifest.json') -Encoding utf8NoBOM
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
    publisher='VulonLok'
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
