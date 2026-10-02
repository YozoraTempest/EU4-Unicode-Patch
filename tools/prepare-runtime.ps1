param([string]$GameDirectory='D:\SteamLibrary\steamapps\common\Europa Universalis IV')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$runtimeRoot=Join-Path $projectRoot 'private\runtime'
$testRoot=Join-Path $projectRoot 'private\test-userdir'
$expected='9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
if ((Get-FileHash (Join-Path $GameDirectory 'eu4.exe') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'This research prototype requires the verified EU4 1.37.5 x64 executable.'
}
if (!(Test-Path (Join-Path $GameDirectory 'version.dll'))) { throw 'The local version.dll plugin loader is required.' }
if (Get-Process eu4 -ErrorAction SilentlyContinue | Where-Object Path -EQ (Join-Path $runtimeRoot 'eu4.exe')) {
    throw 'Close the isolated test instance before preparing its runtime.'
}
New-Item -ItemType Directory -Path $runtimeRoot,$testRoot -Force | Out-Null
& robocopy $GameDirectory $runtimeRoot /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD (Join-Path $GameDirectory 'plugins') /XF d3d9.dll userdir.txt plugin_pattern_log.log console_history.txt
if ($LASTEXITCODE -gt 7) { throw "Runtime copy failed: $LASTEXITCODE" }
New-Item -ItemType Directory -Path (Join-Path $runtimeRoot 'plugins') -Force | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $runtimeRoot 'userdir.txt'),$testRoot.Replace('\','/')+"`n",$utf8)
[IO.File]::WriteAllText((Join-Path $runtimeRoot 'steam_appid.txt'),"236850`n",$utf8)
'Isolated runtime prepared. Source game files were only read.'
