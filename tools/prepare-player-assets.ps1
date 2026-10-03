$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'fetch-open-fonts.ps1')
$manifest=Get-Content -LiteralPath (Join-Path $projectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json
$options=@('--atlas-width','2048','--atlas-height','4096','--require-font-files','1')
foreach ($font in ($manifest.files | Where-Object name -EQ 'SourceHanSansSC-Regular.otf')) {
    $source=Join-Path $projectRoot "private/open-fonts/$($font.name)"
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $font.sha256) {
        throw 'Open font hash mismatch.'
    }
    $options+=@('--font',$source)
}
$destination=Join-Path $projectRoot 'build/player-assets/gfx/fonts/eu4-unicode'
& (Join-Path $projectRoot 'build/fontpack.exe') (Join-Path $projectRoot 'fixtures/player-font-seed.txt') $destination @options
if ($LASTEXITCODE -ne 0) { throw 'Player atlas generation failed; no system glyphs may be distributed.' }
'Player base atlases generated exclusively from the pinned Source Han font.'
