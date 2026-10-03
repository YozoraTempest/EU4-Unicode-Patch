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
    $version=[IO.File]::ReadAllText((Join-Path $releaseProjectRoot 'VERSION')).Trim()
    if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Invalid VERSION.' }
    $commit=Get-SourceCommit
    if ($Channel -eq 'Nightly') {
        if (!$BuildDate) { $BuildDate=[TimeZoneInfo]::ConvertTimeBySystemTimeZoneId([DateTimeOffset]::UtcNow,'China Standard Time').ToString('yyyyMMdd') }
        [void][DateTime]::ParseExact($BuildDate,'yyyyMMdd',[Globalization.CultureInfo]::InvariantCulture)
        $suffix="nightly-$BuildDate-$($commit.Substring(0,7))"
        $tag="v$version-$suffix"
    } elseif ($Channel -eq 'Release') {
        if ($BuildDate) { throw 'BuildDate is only used for nightly packages.' }
        $suffix="v$version"
        $tag=$suffix
    } else { throw 'Unknown release channel.' }
    return [pscustomobject]@{
        Version=$version; Channel=$Channel.ToLowerInvariant(); Tag=$tag
        SourceCommit=$commit; BuildDate=$BuildDate
        PlayerPackage="EU4UnicodePatch-1.37.5-$suffix.zip"
        FontPackage="EU4UnicodePatch-fonts-$suffix.zip"
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-AutomatedTestNames {
    return @('unicode_text','unicode_services','unicode_layout','glyph_registry','unicode_editor',
        'unicode_search','native_search','native_steam_presence','native_script_bom','native_ime',
        'font_assets','font_draw_batches','font_cache','version_proxy')
}
