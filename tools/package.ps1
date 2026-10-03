$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$patchVersion='0.1.0-experimental'
$packageName="EU4UnicodePatch-1.37.5-v$patchVersion-isolated.zip"
$packageRoot=Join-Path $projectRoot 'dist\EU4UnicodePatch'
$fontManifest=Get-Content -LiteralPath (Join-Path $projectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json
$validation=Get-Content -LiteralPath (Join-Path $projectRoot 'docs/evidence/native-dynamic-fonts.json') -Raw | ConvertFrom-Json
$dll=Join-Path $projectRoot 'build/eu4_unicode_probe.dll'
$dllHash=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
if ($dllHash -ne $validation.dll_sha256) { throw 'The package DLL differs from the validated native GPU build.' }

$files=@(
    'README.md','LICENSE','THIRD_PARTY_NOTICES.md',
    'build/eu4_unicode_probe.dll','build/fontpack.exe',
    'tools/prepare-runtime.ps1','tools/prepare-test.ps1','tools/start-test.ps1',
    'tools/fetch-open-fonts.ps1','tools/migrate-localisation.py',
    'fixtures/open-fonts.json','fixtures/localisation/eu4_unicode_probe_l_english.yml',
    'fixtures/events/unicode_probe.txt','fixtures/events/unicode_persistence.txt',
    'docs/install.md','docs/build.md','docs/open-fonts.md','docs/development.md',
    'docs/validation.md','docs/roadmap.md','docs/ci/windows-build.yml',
    "docs/releases/v$patchVersion.md",'third-party/EU4dll-LICENSE.txt',
    'third-party/SourceHanSans-OFL.txt','third-party/Plangothic-OFL.txt',
    'vendor/utfcpp/LICENSE','vendor/minhook/LICENSE.txt'
)
foreach ($relative in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $projectRoot $relative) -PathType Leaf)) {
        throw "Missing package file: $relative"
    }
}
foreach ($font in $fontManifest.files) {
    $source=Join-Path $projectRoot "private/open-fonts/$($font.name)"
    if (!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).Length -ne $font.bytes -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $font.sha256) {
        throw 'Fetch the pinned open fonts before packaging.'
    }
}

# Clear only the checked generated staging directory; never package private
# game resources, source trees, raw evidence or Python caches by recursion.
if (Test-Path -LiteralPath $packageRoot) {
    if ((Resolve-Path -LiteralPath $packageRoot).Path -ne [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist\EU4UnicodePatch'))) {
        throw 'Unexpected generated package path.'
    }
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
foreach ($relative in $files) {
    $target=Join-Path $packageRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $target -Force
}
$fontRoot=Join-Path $packageRoot 'fonts'
New-Item -ItemType Directory -Path $fontRoot -Force | Out-Null
foreach ($font in $fontManifest.files) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "private/open-fonts/$($font.name)") -Destination $fontRoot -Force
}

$packageFiles=@(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path=[IO.Path]::GetRelativePath($packageRoot,$_.FullName).Replace('\','/')
        bytes=$_.Length
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$sourceCommit=& git -C $projectRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Package from a source checkout with a recorded commit.' }
$manifest=[ordered]@{
    patch_version=$patchVersion
    status='experimental'
    distribution='isolated runtime'
    source_commit=$sourceCommit
    game_version='1.37.5.0 Inca Windows x64'
    game_exe_sha256=$validation.exe_sha256
    patch_dll_sha256=$dllHash
    experimental_input_default=$false
    validation='Three controlled native SDL commits and 14 exact 16px GUI GPU glyph regions; other scopes described in docs/validation.md'
    files=$packageFiles
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Encoding utf8

$archive=Join-Path $projectRoot "dist/$packageName"
Compress-Archive -Path $packageRoot -DestinationPath $archive -Force
$archiveHash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $projectRoot 'dist/SHA256SUMS.txt'),
    "$archiveHash  $packageName`n$dllHash  EU4UnicodePatch/build/eu4_unicode_probe.dll`n",[Text.UTF8Encoding]::new($false))
Write-Output $archive
Write-Output "SHA-256: $archiveHash"
