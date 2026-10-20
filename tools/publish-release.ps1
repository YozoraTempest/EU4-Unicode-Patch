param(
    [Parameter(Mandatory)][ValidateSet('Release','Nightly')][string]$Channel,
    [string]$BuildDate='',
    [string]$BuildDirectory='build',
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
$arguments=@((Join-Path $PSScriptRoot 'publish_release.py'),'--channel',$Channel,"--build-date=$BuildDate",
    '--bundle-directory',(Join-Path (Get-BuildRoot $BuildDirectory) 'release-bundle'))
if ($CheckOnly) { $arguments+='--check-only' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Cross-platform release validation or publication failed.' }
