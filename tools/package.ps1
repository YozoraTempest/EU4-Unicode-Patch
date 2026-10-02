$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$packageRoot=Join-Path $projectRoot 'dist\EU4UnicodePatch'
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
foreach ($folder in @('include','src','tests','tools','fixtures','docs','third-party')) {
    Copy-Item (Join-Path $projectRoot $folder) $packageRoot -Recurse -Force
}
foreach ($file in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','CMakeLists.txt','.gitmodules','.gitignore')) {
    Copy-Item (Join-Path $projectRoot $file) $packageRoot -Force
}
$binaryRoot=Join-Path $packageRoot 'build'
New-Item -ItemType Directory -Path $binaryRoot -Force | Out-Null
Copy-Item (Join-Path $projectRoot 'build\eu4_unicode_probe.dll') $binaryRoot -Force
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
