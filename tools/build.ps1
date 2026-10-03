param([switch]$Fresh,[string]$BuildDirectory='build')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Visual Studio Installer / vswhere was not found.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Install the MSVC x64 C++ build tools and a Windows SDK.' }
$envLines = & "$env:ComSpec" /d /s /c "`"$vsRoot\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
$env:VSLANG = '1033'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (!(Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$configure = @('-S',$projectRoot,'-B',$buildRoot,'-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo')
# CMake's localized compiler probe can decode UTF-8 diagnostics as an ANSI
# code page. Obtain the actual prefix so Ninja records header dependencies.
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$probeSource = Join-Path $buildRoot 'dependency-prefix.c'
[IO.File]::WriteAllText($probeSource,"#include <stdio.h>`n",[Text.Encoding]::ASCII)
$probeInfo = [Diagnostics.ProcessStartInfo]::new((Get-Command cl.exe).Source)
$probeInfo.UseShellExecute = $false
$probeInfo.RedirectStandardOutput = $true
$probeInfo.RedirectStandardError = $true
$probeInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
$probeInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
foreach ($argument in @('/nologo','/showIncludes','/c',"/Fo$(Join-Path $buildRoot 'dependency-prefix.obj')",$probeSource)) { $probeInfo.ArgumentList.Add($argument) }
$probe = [Diagnostics.Process]::Start($probeInfo)
$probeOutput = $probe.StandardOutput.ReadToEnd()
$probeError = $probe.StandardError.ReadToEnd()
$probe.WaitForExit()
if ($probe.ExitCode -ne 0) { throw "Compiler dependency probe failed: $probeError" }
$probeLines = $probeOutput -split '\r?\n'
$prefix = $null
foreach ($line in $probeLines) {
    if ([string]$line -match '^(.+?)[A-Za-z]:[\\/].*stdio\.h\s*$') { $prefix = $Matches[1]; break }
}
if (!$prefix) { throw 'Could not determine compiler include prefix.' }
$configure += "-DSHOWINCLUDES_PREFIX_OVERRIDE:STRING=$prefix"
if ($Fresh) { $configure += '--fresh' }
& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $cmake --build $buildRoot
if ($LASTEXITCODE -ne 0) { throw 'Build failed. Exit the isolated game before rebuilding; its debugger may hold the PDB open.' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildRoot --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
