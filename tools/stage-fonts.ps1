$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$stageRoot=Join-Path $projectRoot 'build/player-fonts'
if (Test-Path -LiteralPath $stageRoot) {
    if ((Resolve-Path -LiteralPath $stageRoot).Path -ne [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/player-fonts'))) {
        throw 'Unexpected generated optional-font staging path.'
    }
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
$data=Join-Path $stageRoot 'plugins/eu4_unicode_patch'
New-Item -ItemType Directory -Path (Join-Path $data 'fonts'),(Join-Path $data 'licenses') -Force | Out-Null
$fontManifest=Get-Content -LiteralPath (Join-Path $projectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json
foreach ($font in $fontManifest.files) {
    $source=Join-Path $projectRoot "private/open-fonts/$($font.name)"
    if ((Get-Item -LiteralPath $source).Length -ne $font.bytes -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $font.sha256) {
        throw 'Pinned open font verification failed.'
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $data 'fonts')
}
foreach ($name in @('SourceHanSans-OFL.txt','Plangothic-OFL.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "third-party/$name") -Destination (Join-Path $data 'licenses')
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/optional-fonts.txt') -Destination (Join-Path $stageRoot 'EU4UnicodePatch.FONTS.txt')
$stageRoot
