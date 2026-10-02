param(
    [string]$GameDirectory = 'D:\SteamLibrary\steamapps\common\Europa Universalis IV',
    [string]$FontDirectory = 'D:\SteamLibrary\steamapps\workshop\content\236850\2976470733\gfx\fonts'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $projectRoot 'private\test-userdir'
$modRoot = Join-Path $projectRoot 'private\test-mod'
foreach ($relative in @('localisation\replace','gfx\fonts','interface')) {
    New-Item -ItemType Directory -Path (Join-Path $modRoot $relative) -Force | Out-Null
}
New-Item -ItemType Directory -Path (Join-Path $testRoot 'mod') -Force | Out-Null
$utf8Bom = [Text.UTF8Encoding]::new($true)
$utf8 = [Text.UTF8Encoding]::new($false)
$localization = [IO.File]::ReadAllText((Join-Path $projectRoot 'fixtures\localisation\eu4_unicode_probe_l_english.yml'))
[IO.File]::WriteAllText((Join-Path $modRoot 'localisation\replace\eu4_unicode_probe_l_english.yml'),$localization,$utf8Bom)
# Reuse installed mod fonts only in the private test fixture. They are not packaged.
foreach ($size in @(14,16,18,24)) {
    foreach ($extension in @('fnt','dds')) {
        Copy-Item -LiteralPath (Join-Path $FontDirectory "zh-hans-$size.$extension") -Destination (Join-Path $modRoot 'gfx\fonts')
    }
    # Relocate glyphs which would overlap the engine font object's fields.
    $fontPath = Join-Path $modRoot "gfx\fonts\zh-hans-$size.fnt"
    $fontText = [IO.File]::ReadAllText($fontPath)
    $fontText = [regex]::Replace($fontText, '(?m)^char id=(\d+)\b', {
        param($match)
        $id = [int]$match.Groups[1].Value
        if ($id -ge 0x100 -and $id -lt 0xa00) { 'char id=' + ($id + 0xe000) }
        else { $match.Value }
    })
    [IO.File]::WriteAllText($fontPath,$fontText,$utf8)
}
# Keep the game's existing font definitions and change only the test font paths.
$coreGfx = [IO.File]::ReadAllText((Join-Path $GameDirectory 'interface\core.gfx'))
$coreGfx = [regex]::Replace($coreGfx, 'gfx/fonts/(vic_(18|22|29|36)[^"\r\n]*|garamond_(14|16|24)[^"\r\n]*|Arial12)', {
    param($match)
    $size = switch ($match.Groups[2].Value + $match.Groups[3].Value) {
        '18' { 16 }; '22' { 18 }; '29' { 24 }; '36' { 24 }
        '16' { 16 }; '24' { 24 }; default { 14 }
    }
    "gfx/fonts/zh-hans-$size"
})
[IO.File]::WriteAllText((Join-Path $modRoot 'interface\core.gfx'),$coreGfx,$utf8)
$frontend = [IO.File]::ReadAllText((Join-Path $GameDirectory 'interface\frontend.gui'))
$probePanel = @'
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
