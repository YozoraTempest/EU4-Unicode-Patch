param(
    [string]$GameDirectory = 'D:\SteamLibrary\steamapps\common\Europa Universalis IV',
    [string]$FontDirectory = 'D:\SteamLibrary\steamapps\workshop\content\236850\2976470733\gfx\fonts',
    [switch]$SystemFonts,
    [switch]$OpenFonts,
    [switch]$SupplementarySaveProbe,
    [switch]$PersistenceProbe,
    [switch]$SearchProbe,
    [string]$MigratedLocalisationDirectory
)
$ErrorActionPreference = 'Stop'
if ($OpenFonts) { $SystemFonts=$true }
$projectRoot = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $projectRoot 'private\test-userdir'
$modRoot = Join-Path $projectRoot 'private\test-mod'
$runtimeExe = Join-Path $projectRoot 'private\runtime\eu4.exe'
if (Get-CimInstance Win32_Process -Filter "Name='eu4.exe'" | Where-Object { $_.ExecutablePath -eq $runtimeExe }) {
    throw 'Exit the isolated game before changing its localization or font fixture.'
}
$migratedFiles = @()
if (@($SupplementarySaveProbe,$PersistenceProbe,$SearchProbe).Where({ $_ }).Count -gt 1) {
    throw 'Choose one dedicated save-name, persistence or search fixture.'
}
if ($SupplementarySaveProbe -and !$SystemFonts) {
    throw 'The supplementary save-name probe needs the system font atlas.'
}
if ($PersistenceProbe -and !$SystemFonts) {
    throw 'The persistence probe needs the system font atlas.'
}
if ($SearchProbe -and !$SystemFonts) {
    throw 'The multilingual search probe needs the system font atlas.'
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
if ($PersistenceProbe) {
    $localization = $localization.Replace(' FRA:0 "法兰西"',' FRA:0 "持久化𠀀"')
}
if ($SearchProbe) {
    $localization = $localization.Replace(' ENG:0 "英格兰"',' ENG:0 "École Straße Ａ 英格兰𠀀"')
    $localization = $localization.Replace(' CAS:0 "卡斯蒂利亚"',' CAS:0 "ΕΛΛΑΔΑ МОСКВА 卡斯蒂利亚"')
}
[IO.File]::WriteAllText((Join-Path $modRoot 'localisation\replace\eu4_unicode_probe_l_english.yml'),$localization,$utf8Bom)
Copy-Item -LiteralPath (Join-Path $projectRoot 'fixtures\events\unicode_probe.txt') -Destination (Join-Path $modRoot 'events') -Force
$persistenceEvent = Join-Path $modRoot 'events\unicode_persistence.txt'
$countryFixture = Join-Path $modRoot 'common\countries\France.txt'
foreach ($previousFixture in @($persistenceEvent,$countryFixture)) {
    if (Test-Path -LiteralPath $previousFixture) {
        if (!(Resolve-Path -LiteralPath $previousFixture).Path.StartsWith($modRoot + '\',[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Unexpected generated persistence fixture path.'
        }
        Remove-Item -LiteralPath $previousFixture -Force
    }
}
if ($PersistenceProbe) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'fixtures\events\unicode_persistence.txt') -Destination $persistenceEvent
    # This vanilla script is CP1252. Convert its private copy explicitly to
    # UTF-8, then replace only the two native unit-name templates.
    $country = [Text.Encoding]::GetEncoding(1252).GetString([IO.File]::ReadAllBytes((Join-Path $GameDirectory 'common\countries\France.txt')))
    $country = [regex]::Replace($country,'(?s)army_names\s*=\s*\{[^}]*\}','army_names = { "中文𠀀测试军" }')
    $country = [regex]::Replace($country,'(?s)fleet_names\s*=\s*\{[^}]*\}','fleet_names = { "中文😀测试舰队" }')
    New-Item -ItemType Directory -Path (Split-Path $countryFixture -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($countryFixture,$country,$utf8)
}
# Trigger the dedicated event for the human country when a fixture campaign
# starts. Preserve every original on_action in this private generated copy.
$actions = [IO.File]::ReadAllText((Join-Path $GameDirectory 'common\on_actions\00_on_actions.txt'))
$actions = [regex]::new('on_startup\s*=\s*\{').Replace($actions, "on_startup = {`n if = { limit = { ai = no } country_event = { id = eu4_unicode.1 } }", 1)
if ($PersistenceProbe) {
    $actions = [regex]::new('on_startup\s*=\s*\{').Replace($actions, "on_startup = {`n if = { limit = { ai = no NOT = { has_country_flag = eu4_unicode_persistence_initialized } } country_event = { id = eu4_unicode.2 } }", 1)
}
[IO.File]::WriteAllText((Join-Path $modRoot 'common\on_actions\00_on_actions.txt'),$actions,$utf8)
# Reuse installed mod fonts only in the private test fixture. They are not packaged.
if ($SystemFonts) {
    $fontOptions=@()
    if ($OpenFonts) {
        & (Join-Path $PSScriptRoot 'fetch-open-fonts.ps1')
        $manifest=Get-Content -LiteralPath (Join-Path $projectRoot 'fixtures/open-fonts.json') -Raw | ConvertFrom-Json
        foreach ($font in $manifest.files) { $fontOptions+=@('--font',(Join-Path $projectRoot "private/open-fonts/$($font.name)")) }
        $fontOptions+=@('--atlas-width','2048','--atlas-height','4096')
    }
    & (Join-Path $projectRoot 'build\fontpack.exe') (Join-Path $modRoot 'localisation\replace\eu4_unicode_probe_l_english.yml') (Join-Path $modRoot 'gfx\fonts') @fontOptions
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
