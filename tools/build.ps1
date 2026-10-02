$ErrorActionPreference = 'Stop'
# Keep Ninja's /showIncludes dependency prefix independent of console encoding.
$env:VSLANG = '1033'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$envLines = & "$env:ComSpec" /d /s /c "`"$vsRoot\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --fresh -S $projectRoot -B (Join-Path $projectRoot 'build') -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $cmake --build (Join-Path $projectRoot 'build')
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir (Join-Path $projectRoot 'build') --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
