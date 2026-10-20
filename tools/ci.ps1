param(
    [ValidateSet('Release','Nightly')][string]$Channel='Release',
    [string]$BuildDate='',
    [string]$BuildDirectory='build'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
& (Join-Path $PSScriptRoot 'build.ps1') -Fresh -SkipTests -BuildDirectory $BuildDirectory
& (Join-Path $PSScriptRoot 'fetch-open-fonts.ps1')
& (Join-Path $PSScriptRoot 'test-build.ps1') -BuildDirectory $BuildDirectory
& (Join-Path $PSScriptRoot 'package.ps1') -Channel $Channel -BuildDate $BuildDate -BuildDirectory $BuildDirectory

& python (Join-Path $PSScriptRoot 'package-diagnostics.py') --platform windows --build-directory $BuildDirectory --channel $Channel "--build-date=$BuildDate"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic packaging failed.' }
& python (Join-Path (Split-Path $PSScriptRoot -Parent) 'tests/diagnostics_package_test.py') --platform windows --stage (Join-Path (Get-BuildRoot $BuildDirectory) 'diagnostics-windows')
if ($LASTEXITCODE -ne 0) { throw 'Independent diagnostic package tests failed.' }
