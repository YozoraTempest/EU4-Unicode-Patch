$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$destination=Join-Path $projectRoot 'private/open-fonts'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$manifest=Get-Content -LiteralPath (Join-Path $projectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json
foreach ($font in $manifest.files) {
    $target=Join-Path $destination $font.name
    if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -eq $font.sha256) { continue }
    $packaged=Join-Path $projectRoot "fonts/$($font.name)"
    if (Test-Path -LiteralPath $packaged) {
        if ((Get-FileHash -LiteralPath $packaged).Hash.ToLowerInvariant() -ne $font.sha256) { throw 'Packaged font checksum mismatch' }
        Copy-Item -LiteralPath $packaged -Destination $target -Force
        continue
    }
    if ($font.archive) {
        $archive=Join-Path $destination $font.archive
        if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() -ne $font.archive_sha256) {
            Invoke-WebRequest -Uri $font.url -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() -ne $font.archive_sha256) { throw 'Font archive checksum mismatch' }
        $zip=[IO.Compression.ZipFile]::OpenRead($archive)
        try {
            $entry=$zip.GetEntry($font.entry)
            if (!$entry) { throw 'Pinned font archive entry is missing' }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$true)
        } finally { $zip.Dispose() }
    } else { Invoke-WebRequest -Uri $font.url -OutFile $target }
    if ((Get-Item -LiteralPath $target).Length -ne $font.bytes -or
        (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -ne $font.sha256) { throw 'Font checksum mismatch' }
}
Write-Output 'Pinned open fonts are available in private/open-fonts; no system fonts or registry settings were changed.'
