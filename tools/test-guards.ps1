param([string]$Configuration='',[string]$BuildDirectory='build')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
if ($Configuration) { $buildRoot=Join-Path $buildRoot $Configuration }
$dll=Join-Path $buildRoot 'eu4_unicode_probe.dll'
$log=Join-Path $buildRoot 'eu4_unicode_probe.log'
& (Join-Path $buildRoot 'guard_host.exe') $dll
if ($LASTEXITCODE -ne 0) { throw 'Guard test host failed' }
if ((Get-Content $log -Raw) -notmatch 'outside the isolated research fixture') { throw 'Path guard did not reject host' }
$layoutFixture=Join-Path $buildRoot 'guard-case\EU4UnicodePatch\private\runtime'
New-Item -ItemType Directory -Path $layoutFixture -Force | Out-Null
$hostPath=Join-Path $layoutFixture 'guard_host.exe'
Copy-Item (Join-Path $buildRoot 'guard_host.exe') $hostPath
& $hostPath $dll
if ($LASTEXITCODE -ne 0) { throw 'Layout test host failed' }
$diagnostic=Get-Content $log -Raw
if ($diagnostic -notmatch 'Executable SHA-256: [0-9a-f]{64}' -or $diagnostic -notmatch 'unsupported section layout') {
    throw 'Layout guard did not reject host after recording its diagnostic hash.'
}
$playerDll=Join-Path $buildRoot 'eu4_unicode_patch.dll'
& (Join-Path $buildRoot 'guard_host.exe') $playerDll
if ($LASTEXITCODE -ne 0 -or (Get-Content (Join-Path $buildRoot 'eu4_unicode_patch.log') -Raw) -notmatch 'unsupported section layout') {
    throw 'Player DLL did not reject an unsupported executable.'
}
$loaderFixture=Join-Path $buildRoot 'player-loader-case'
$plugins=Join-Path $loaderFixture 'plugins'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'version_proxy_tests.exe') -Destination (Join-Path $loaderFixture 'eu4.exe') -Force
Copy-Item -LiteralPath (Join-Path $buildRoot 'VERSION.dll') -Destination $loaderFixture -Force
Copy-Item -LiteralPath $playerDll -Destination $plugins -Force
foreach ($name in @('plugin64.dll','eu4_unicode_probe.dll','eu4_menu_patch.dll')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot 'loader_fixture.dll') -Destination (Join-Path $plugins $name) -Force
}
& (Join-Path $loaderFixture 'eu4.exe') (Join-Path $loaderFixture 'VERSION.dll')
if ($LASTEXITCODE -ne 0) { throw 'Player loader conflict/neighbor test failed.' }
'PASS: research isolation, both executable guards, system forwarding and player loader conflict/neighbor checks.'
