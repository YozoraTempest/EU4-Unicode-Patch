param([string]$GameDirectory='D:\SteamLibrary\steamapps\common\Europa Universalis IV',[string]$SaveFile='',[switch]$OptionalFonts)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$runtime=Join-Path $projectRoot 'private/player-install/Europa Universalis IV'
$userdir=Join-Path $projectRoot 'private/player-userdir'
$exe=Join-Path $runtime 'eu4.exe'
if ((Get-FileHash -LiteralPath (Join-Path $GameDirectory 'eu4.exe')).Hash.ToLowerInvariant() -ne
    '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a') { throw 'Unexpected source game executable.' }
if (Get-Process eu4 -ErrorAction SilentlyContinue | Where-Object Path -EQ $exe) { throw 'Close the owned player test first.' }
New-Item -ItemType Directory -Path $runtime,$userdir -Force | Out-Null
& robocopy $GameDirectory $runtime /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD (Join-Path $GameDirectory 'plugins') /XF VERSION.dll d3d9.dll userdir.txt plugin_pattern_log.log console_history.txt
if ($LASTEXITCODE -gt 7) { throw 'Player fixture copy failed.' }
# Retain the source installation's old plugins to test overlay conflict
# handling. The source directory is never written to.
New-Item -ItemType Directory -Path (Join-Path $runtime 'plugins') -Force | Out-Null
foreach ($name in @('plugin64.dll','Plugin.dll','eu4_menu_patch.dll','autoupdate64.bat','dllautoupdater.exe')) {
    $source=Join-Path $GameDirectory "plugins/$name"
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $runtime 'plugins') -Force }
}
& robocopy (Join-Path $projectRoot 'build/player-package') $runtime /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -gt 7) { throw 'Player overlay copy failed.' }
$testFonts=Join-Path $runtime 'plugins/eu4_unicode_patch/fonts'
if (Test-Path -LiteralPath $testFonts) {
    $resolvedFonts=(Resolve-Path -LiteralPath $testFonts).Path
    if ($resolvedFonts -ne [IO.Path]::GetFullPath($testFonts) -or
        !$resolvedFonts.StartsWith([IO.Path]::GetFullPath($runtime)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unexpected owned test font path.'
    }
    Remove-Item -LiteralPath $resolvedFonts -Recurse -Force
}
if ($OptionalFonts) {
    & (Join-Path $PSScriptRoot 'stage-fonts.ps1') | Out-Null
    & robocopy (Join-Path $projectRoot 'build/player-fonts') $runtime /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -gt 7) { throw 'Optional test font copy failed.' }
}
[IO.File]::WriteAllText((Join-Path $runtime 'userdir.txt'),$userdir.Replace('\','/'),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $runtime 'steam_appid.txt'),'236850',[Text.Encoding]::ASCII)
Copy-Item -LiteralPath (Join-Path $projectRoot 'fixtures/player-settings.txt') -Destination (Join-Path $userdir 'settings.txt') -Force
[IO.File]::WriteAllText((Join-Path $userdir 'dlc_load.json'),'{"disabled_dlcs":[],"enabled_mods":[]}',[Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path (Join-Path $userdir 'save games') -Force | Out-Null
if ($SaveFile) {
    $name=Split-Path $SaveFile -Leaf
    Copy-Item -LiteralPath $SaveFile -Destination (Join-Path $userdir 'save games') -Force
    [ordered]@{title='Player test';desc='';filename="save games/$name"} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $userdir 'continue_game.json') -Encoding utf8NoBOM
}
'Player overlay prepared in an ordinary-named, owned game copy with vanilla font definitions and no enabled mods.'
