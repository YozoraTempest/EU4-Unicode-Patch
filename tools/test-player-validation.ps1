param([string]$BuildDirectory='build')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=Get-BuildRoot $BuildDirectory
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
    @{Path='eu4_unicode_patch.dll';Hash=$validation.dll_sha256},
    @{Path='VERSION.dll';Hash=$validation.loader_sha256}
)) {
    if ((Get-FileHash -LiteralPath (Join-Path $buildRoot $entry.Path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Hash) {
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
'PASS: player runtime evidence matches these binaries.'
