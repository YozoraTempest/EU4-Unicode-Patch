param(
    [ValidateSet('Release','Nightly')][string]$Channel='Release',
    [string]$BuildDate='',
    [string]$BuildDirectory='build'
)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -Fresh -SkipTests -BuildDirectory $BuildDirectory
& (Join-Path $PSScriptRoot 'fetch-open-fonts.ps1')
& (Join-Path $PSScriptRoot 'test-build.ps1') -BuildDirectory $BuildDirectory
& (Join-Path $PSScriptRoot 'package.ps1') -Channel $Channel -BuildDate $BuildDate -BuildDirectory $BuildDirectory
