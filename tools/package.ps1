$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$packageRoot=Join-Path $projectRoot 'dist\EU4UnicodePatch'
# Remove only the previous generated package, after checking its resolved path.
if (Test-Path -LiteralPath $packageRoot) {
    if ((Resolve-Path -LiteralPath $packageRoot).Path -ne [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist\EU4UnicodePatch'))) {
        throw 'Unexpected generated package path.'
    }
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
foreach ($folder in @('include','src','tests','tools','fixtures','docs','third-party')) {
    Copy-Item (Join-Path $projectRoot $folder) $packageRoot -Recurse -Force
}
foreach ($file in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','CMakeLists.txt','.gitmodules','.gitignore')) {
    Copy-Item (Join-Path $projectRoot $file) $packageRoot -Force
}
$binaryRoot=Join-Path $packageRoot 'build'
New-Item -ItemType Directory -Path $binaryRoot -Force | Out-Null
foreach ($binary in @('eu4_unicode_probe.dll','fontpack.exe')) {
    Copy-Item (Join-Path $projectRoot "build\$binary") $binaryRoot -Force
}
$utfcppRoot=Join-Path $packageRoot 'vendor\utfcpp'
$minhookRoot=Join-Path $packageRoot 'vendor\minhook'
New-Item -ItemType Directory -Path $utfcppRoot,$minhookRoot -Force | Out-Null
Copy-Item (Join-Path $projectRoot 'vendor\utfcpp\source') $utfcppRoot -Recurse -Force
Copy-Item (Join-Path $projectRoot 'vendor\utfcpp\LICENSE') $utfcppRoot -Force
foreach ($folder in @('include','src')) {
    Copy-Item (Join-Path $projectRoot "vendor\minhook\$folder") $minhookRoot -Recurse -Force
}
foreach ($file in @('CMakeLists.txt','LICENSE.txt')) {
    Copy-Item (Join-Path $projectRoot "vendor\minhook\$file") $minhookRoot -Force
}
$archive=Join-Path $projectRoot 'dist\EU4UnicodePatch-utf8-prototype-1.37.5-x64.zip'
Compress-Archive -Path $packageRoot -DestinationPath $archive -Force
Get-FileHash $archive -Algorithm SHA256 | Format-List
