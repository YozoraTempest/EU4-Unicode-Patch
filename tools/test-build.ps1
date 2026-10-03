param([string]$BuildDirectory='build')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
Assert-CleanCheckout
$root=Get-BuildRoot $BuildDirectory
$info=Get-Content -LiteralPath (Join-Path $root 'build-info.json') -Raw | ConvertFrom-Json
if ($info.source_tree_dirty -or $info.source_commit -ne (Get-SourceCommit) -or
    $info.patch_dll_sha256 -ne (Get-Sha256 (Join-Path $root 'eu4_unicode_patch.dll')) -or
    $info.loader_sha256 -ne (Get-Sha256 (Join-Path $root 'VERSION.dll'))) {
    throw 'Rebuild the current clean checkout before validation.'
}
$reportPath=Join-Path $root 'automated-validation.json'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath }
$ctest=(Get-Command ctest -ErrorAction SilentlyContinue).Source
if (!$ctest) {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $ctest=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
}
& $ctest --test-dir $root --output-on-failure --output-junit (Join-Path $root 'ctest-results.xml')
if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
[xml]$junit=Get-Content -LiteralPath (Join-Path $root 'ctest-results.xml') -Raw
$expected=@('unicode_text','unicode_services','unicode_layout','glyph_registry','unicode_editor','unicode_search','native_steam_presence','native_ime','font_assets','font_draw_batches','font_cache','version_proxy')
$actual=@($junit.testsuite.testcase | ForEach-Object { $_.name })
if ((Compare-Object $expected $actual) -or [int]$junit.testsuite.failures -ne 0 -or [int]$junit.testsuite.skipped -ne 0) {
    throw 'The complete CTest suite must pass without skipped tests.'
}
& (Join-Path $PSScriptRoot 'test-guards.ps1') -BuildDirectory $BuildDirectory
& (Join-Path $root 'open_font_tests.exe') (Join-Path $releaseProjectRoot 'private/open-fonts') (Join-Path $root 'open-font-preview.png') |
    Tee-Object -FilePath (Join-Path $root 'open-font-check.log')
if ($LASTEXITCODE -ne 0) { throw 'Pinned optional-font fallback tests failed.' }
if ($info.patch_dll_sha256 -ne (Get-Sha256 (Join-Path $root 'eu4_unicode_patch.dll')) -or
    $info.loader_sha256 -ne (Get-Sha256 (Join-Path $root 'VERSION.dll'))) { throw 'Binaries changed during validation.' }
[ordered]@{
    source_commit=$info.source_commit
    patch_dll_sha256=$info.patch_dll_sha256
    loader_sha256=$info.loader_sha256
    passed=$true
    ctest=$actual
    loader_guards=$true
    optional_fonts=$true
    game_runtime='not tested by CI'
} | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM
