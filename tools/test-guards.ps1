param([string]$Configuration='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=Join-Path $projectRoot 'build'
if ($Configuration) { $buildRoot=Join-Path $buildRoot $Configuration }
$dll=Join-Path $buildRoot 'eu4_unicode_probe.dll'
$log=Join-Path $buildRoot 'eu4_unicode_probe.log'
& (Join-Path $buildRoot 'guard_host.exe') $dll
if ($LASTEXITCODE -ne 0) { throw 'Guard test host failed' }
if ((Get-Content $log -Raw) -notmatch 'outside the isolated research fixture') { throw 'Path guard did not reject host' }
$hashFixture=Join-Path $projectRoot 'build\guard-case\EU4UnicodePatch\private\runtime'
New-Item -ItemType Directory -Path $hashFixture -Force | Out-Null
$hostPath=Join-Path $hashFixture 'guard_host.exe'
Copy-Item (Join-Path $buildRoot 'guard_host.exe') $hostPath
& $hostPath $dll
if ($LASTEXITCODE -ne 0) { throw 'Hash test host failed' }
if ((Get-Content $log -Raw) -notmatch 'executable hash mismatch') { throw 'Hash guard did not reject host' }
'PASS: DLL rejected both an unrelated directory and a different executable hash before installing hooks.'
