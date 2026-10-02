param(
    [string]$GameDirectory = 'D:\SteamLibrary\steamapps\common\Europa Universalis IV',
    [string]$FontDirectory = 'D:\SteamLibrary\steamapps\workshop\content\236850\2976470733\gfx\fonts',
    [switch]$SystemFonts,
    [switch]$SupplementarySaveProbe,
    [string]$MigratedLocalisationDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $projectRoot 'private\test-userdir'
$modRoot = Join-Path $projectRoot 'private\test-mod'
$runtimeExe = Join-Path $projectRoot 'private\runtime\eu4.exe'
if (Get-CimInstance Win32_Process -Filter "Name='eu4.exe'" | Where-Object { $_.ExecutablePath -eq $runtimeExe }) {
    throw 'Exit the isolated game before changing its localization or font fixture.'
}
$migratedFiles = @()
if ($SupplementarySaveProbe -and !$SystemFonts) {
    throw 'The supplementary save-name probe needs the system font atlas.'
}
if ($MigratedLocalisationDirectory) {
    $migrationRoot = (Resolve-Path -LiteralPath $MigratedLocalisationDirectory).Path
    $report = Get-Content -LiteralPath (Join-Path $migrationRoot 'unicode-migration.json') -Raw | ConvertFrom-Json
    if ($report.format -ne 'EU4dll-escaped-CP1252-in-UTF8') { throw 'Unknown localization migration report.' }
    foreach ($record in $report.records) {
        $path = [IO.Path]::GetFullPath((Join-Path $migrationRoot $record.file))
        if (!$path.StartsWith($migrationRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetExtension($path) -ne '.yml' -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.utf8_sha256) {
            throw 'Migrated localization path or checksum mismatch.'
        }
        $migratedFiles += @{ Path=$path; Relative=$record.file }
    }
    if ($SystemFonts) { throw 'Full migrated localization needs its existing font coverage; system atlas mode currently covers the probe only.' }
}
foreach ($relative in @('localisation\replace','gfx\fonts','interface','events','common\on_actions')) {
    New-Item -ItemType Directory -Path (Join-Path $modRoot $relative) -Force | Out-Null
}
New-Item -ItemType Directory -Path (Join-Path $testRoot 'mod') -Force | Out-Null
# This localization directory belongs exclusively to the generated test mod.
# Clear files from a previous optional migration so returning to probe mode is
# reproducible. Never remove files from the source mod or user directories.
$localizationRoot = Join-Path $modRoot 'localisation'
if ((Resolve-Path -LiteralPath $localizationRoot).Path -ne [IO.Path]::GetFullPath($localizationRoot)) {
    throw 'Unexpected generated localization directory.'
}
Get-ChildItem -LiteralPath $localizationRoot -Recurse -File -Filter '*.yml' | ForEach-Object {
    if (!$_.FullName.StartsWith($localizationRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected generated localization file.' }
    Remove-Item -LiteralPath $_.FullName -Force
}
foreach ($file in $migratedFiles) {
    $target = [IO.Path]::GetFullPath((Join-Path $localizationRoot $file.Relative))
    if (!$target.StartsWith($localizationRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Migrated file escapes the generated localization directory.' }
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $file.Path -Destination $target -Force
}
$utf8Bom = [Text.UTF8Encoding]::new($true)
$utf8 = [Text.UTF8Encoding]::new($false)
$localization = [IO.File]::ReadAllText((Join-Path $projectRoot 'fixtures\localisation\eu4_unicode_probe_l_english.yml'))
if ($SupplementarySaveProbe) {
    $localization = $localization.Replace(' FRA:0 "法兰西"',' FRA:0 "法兰西𠀀"')
}
[IO.File]::WriteAllText((Join-Path $modRoot 'localisation\replace\eu4_unicode_probe_l_english.yml'),$localization,$utf8Bom)
Copy-Item -LiteralPath (Join-Path $projectRoot 'fixtures\events\unicode_probe.txt') -Destination (Join-Path $modRoot 'events') -Force
# Trigger the dedicated event for the human country when a fixture campaign
# starts. Preserve every original on_action in this private generated copy.
$actions = [IO.File]::ReadAllText((Join-Path $GameDirectory 'common\on_actions\00_on_actions.txt'))
$actions = [regex]::new('on_startup\s*=\s*\{').Replace($actions, "on_startup = {`n if = { limit = { ai = no } country_event = { id = eu4_unicode.1 } }", 1)
[IO.File]::WriteAllText((Join-Path $modRoot 'common\on_actions\00_on_actions.txt'),$actions,$utf8)
# Reuse installed mod fonts only in the private test fixture. They are not packaged.
if ($SystemFonts) {
    & (Join-Path $projectRoot 'build\fontpack.exe') (Join-Path $modRoot 'localisation\replace\eu4_unicode_probe_l_english.yml') (Join-Path $modRoot 'gfx\fonts')
    if ($LASTEXITCODE -ne 0) { throw 'System font atlas generation failed.' }
} else {
    foreach ($size in @(14,16,18,24,'map')) {
        foreach ($extension in @('fnt','dds')) {
            Copy-Item -LiteralPath (Join-Path $FontDirectory "zh-hans-$size.$extension") -Destination (Join-Path $modRoot 'gfx\fonts') -Force
        }
    }
}
# Keep the game's existing font definitions and change only the test font paths.
$coreGfx = [IO.File]::ReadAllText((Join-Path $GameDirectory 'interface\core.gfx'))
$coreGfx = [regex]::Replace($coreGfx, 'gfx/fonts/(Mapfont|standard[^"\r\n]*|tahoma_20_bold|vic_(18|22|29|36)[^"\r\n]*|garamond_(14|16|24)[^"\r\n]*|Arial12)', {
    param($match)
    $size = switch ($match.Groups[2].Value + $match.Groups[3].Value) {
        '18' { 16 }; '22' { 18 }; '29' { 24 }; '36' { 24 }
        '16' { 16 }; '24' { 24 }; default { 14 }
    }
    if ($match.Groups[1].Value -eq 'Mapfont') { $size = 'map' }
    "gfx/fonts/zh-hans-$size"
})
[IO.File]::WriteAllText((Join-Path $modRoot 'interface\core.gfx'),$coreGfx,$utf8)
$frontend = [IO.File]::ReadAllText((Join-Path $GameDirectory 'interface\frontend.gui'))
$probePanel = @'
instantTextBoxType = {
 name = "unicode_format_probe"
 position = { x = 470 y = 485 }
 font = "vic_22"
 text = "EU4_UNICODE_FORMAT_PROBE"
 maxWidth = 700
 maxHeight = 32
 fixedsize = yes
 alwaystransparent = yes
}
instantTextBoxType = {
 name = "unicode_collision_probe"
 position = { x = 470 y = 525 }
 font = "vic_22"
 text = "EU4_UNICODE_COLLISION_PROBE"
 maxWidth = 700
 maxHeight = 32
 fixedsize = yes
 alwaystransparent = yes
}
instantTextBoxType = {
 name = "unicode_plain_probe"
 position = { x = 470 y = 250 }
 font = "vic_22"
 text = "EU4_UNICODE_PROBE"
 maxWidth = 570
 maxHeight = 32
 fixedsize = yes
 alwaystransparent = yes
}
instantTextBoxType = {
 name = "unicode_wrap_probe"
 position = { x = 470 y = 295 }
 font = "vic_22"
 text = "EU4_UNICODE_WRAP_PROBE"
 maxWidth = 390
 maxHeight = 155
 fixedsize = yes
 alwaystransparent = yes
}
instantTextBoxType = {
 name = "unicode_scalar_probe"
 position = { x = 470 y = 445 }
 font = "vic_22"
 text = "EU4_UNICODE_SCALAR_PROBE"
 maxWidth = 480
 maxHeight = 32
 fixedsize = yes
 alwaystransparent = yes
}
'@
$frontend = $frontend.Replace('### MAIN MENU PANEL (upperleft)', $probePanel + "`n### MAIN MENU PANEL (upperleft)")
[IO.File]::WriteAllText((Join-Path $modRoot 'interface\frontend.gui'),$frontend,$utf8)
$descriptor = "name=`"EU4 Unicode Prototype Test`"`npath=`"$($modRoot.Replace('\','/'))`"`nsupported_version=`"1.37.5`"`n"
[IO.File]::WriteAllText((Join-Path $testRoot 'mod\utf8-probe.mod'),$descriptor,$utf8)
[IO.File]::WriteAllText((Join-Path $testRoot 'dlc_load.json'),'{"enabled_mods":["mod/utf8-probe.mod"],"disabled_dlcs":[]}',$utf8)
Write-Output "Test localization and private font fixture prepared: $modRoot"
