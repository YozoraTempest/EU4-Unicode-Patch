$releaseProjectRoot=Split-Path $PSScriptRoot -Parent

function Get-BuildRoot([string]$BuildDirectory) {
    $root=[IO.Path]::GetFullPath((Join-Path $releaseProjectRoot $BuildDirectory))
    if (!$root.StartsWith($releaseProjectRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build directory must be inside the source checkout.'
    }
    return $root
}

function Get-SourceCommit {
    $commit=& git -C $releaseProjectRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Cannot read the source commit.' }
    return $commit
}

function Assert-CleanCheckout {
    $changes=& git -C $releaseProjectRoot status --porcelain
    if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Commit source changes before validating or packaging.' }
}

function Get-ReleaseInfo([string]$Channel,[string]$BuildDate='') {
    $result=& python (Join-Path $PSScriptRoot 'release_info.py') --channel $Channel "--build-date=$BuildDate"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine the release identity.' }
    return ($result | ConvertFrom-Json)
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-AutomatedTestNames {
    return @('diagnostic_probe','native_yaml','yaml_input','search_resources','native_editor_select_all','native_mod_registry','native_name_order','localized_format','paragraph_transport','executable_compatibility','executable_profile','unicode_text','unicode_services','formatted_text','unicode_layout','shaped_paragraph','formatted_paragraph','native_paragraph','glyph_registry','unicode_editor',
        'unicode_search','native_search','pinyin_fuzzy','native_steam_presence','native_script_bom','legacy_text','legacy_migration','native_ime',
        'font_assets','font_draw_batches','font_cache','version_proxy')
}
