param([switch]$ExperimentalInput)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$runtimeRoot=Join-Path $projectRoot 'private\runtime'
$exe=Join-Path $runtimeRoot 'eu4.exe'
if (!(Test-Path $exe)) { throw 'Run tools/prepare-runtime.ps1 first.' }
if (Get-Process eu4 -ErrorAction SilentlyContinue | Where-Object Path -EQ $exe) {
    throw 'The isolated test instance is already running.'
}
if ((Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a') {
    throw 'Unexpected isolated executable hash.'
}
if (Test-Path (Join-Path $runtimeRoot 'plugins\plugin64.dll')) { throw 'Remove the legacy plugin from this isolated fixture before testing.' }
New-Item -ItemType Directory -Path (Join-Path $runtimeRoot 'plugins') -Force | Out-Null
Copy-Item (Join-Path $projectRoot 'build\eu4_unicode_probe.dll') (Join-Path $runtimeRoot 'plugins\eu4_unicode_probe.dll')
$inputValue = if ($ExperimentalInput) { 1 } else { 0 }
[IO.File]::WriteAllText((Join-Path $runtimeRoot 'plugins\eu4_unicode_probe.ini'),"[experimental]`nunicode_input=$inputValue`n",[Text.Encoding]::ASCII)
Start-Process -FilePath $exe -WorkingDirectory $runtimeRoot -ArgumentList '-debug' -WindowStyle Hidden
