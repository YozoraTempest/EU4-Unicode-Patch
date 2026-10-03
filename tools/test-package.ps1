param([string]$BuildDirectory='build')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
$root=Get-BuildRoot $BuildDirectory
$info=Get-Content -LiteralPath (Join-Path $root 'package-info.json') -Raw | ConvertFrom-Json
$dist=Join-Path $releaseProjectRoot 'dist'
$checksums=Get-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt')
$fonts=(Get-Content -LiteralPath (Join-Path $releaseProjectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json).files
foreach ($package in @(
    @{Name=$info.PlayerPackage;Stage='player-package';Files=@('VERSION.dll','plugins/eu4_unicode_patch.dll','plugins/eu4_unicode_patch/LICENSE.txt','EU4UnicodePatch.README.txt')},
    @{Name=$info.FontPackage;Stage='player-fonts';Files=@('EU4UnicodePatch.FONTS.txt','plugins/eu4_unicode_patch/FONT_LICENSES.txt','plugins/eu4_unicode_patch/font-manifest.json')+@($fonts | ForEach-Object { "plugins/eu4_unicode_patch/fonts/$($_.name)" })}
)) {
    $archive=Join-Path $dist $package.Name
    if ("$(Get-Sha256 $archive)  $($package.Name)" -cnotin $checksums) { throw 'Archive checksum does not match SHA256SUMS.txt.' }
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entries=@($zip.Entries | Where-Object { $_.Name })
        $names=@($entries | ForEach-Object { $_.FullName.Replace('\','/') })
        if ($names.Count -ne $package.Files.Count -or (Compare-Object $package.Files $names -CaseSensitive)) {
            throw "Unexpected player package contents: $($package.Name)"
        }
        foreach ($entry in $entries) {
            $name=$entry.FullName.Replace('\','/')
            $stream=$entry.Open()
            try { $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant() }
            finally { $stream.Dispose() }
            if ($hash -ne (Get-Sha256 (Join-Path (Join-Path $root $package.Stage) $name))) { throw "ZIP payload differs: $name" }
            if ($name.EndsWith('.txt') -and $name -match 'README|FONTS') {
                $reader=[IO.StreamReader]::new($entry.Open())
                try { $text=$reader.ReadToEnd() } finally { $reader.Dispose() }
                if ($text.Contains('@PATCH_RELEASE@') -or $text.Contains('@FONT_PACKAGE@')) { throw 'Unexpanded package documentation.' }
            }
        }
    } finally { $zip.Dispose() }
}
'PASS: drop-in paths, four-file main package, optional fonts and archive checksums.'
