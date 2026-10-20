param([string]$GameDirectory='', [string]$OutputDirectory='.')
# Compatible with the Windows PowerShell 5.1 included in Windows 10/11.
$ErrorActionPreference='Stop'
$checks=New-Object 'System.Collections.Generic.List[object]'
function Add-Check([string]$Id,[string]$Status,$Detail,[string]$Remedy='') {
    $checks.Add([ordered]@{id=$Id;status=$Status;detail=$Detail;remedy=$Remedy})
}
function Get-Hash([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose();$stream.Dispose() }
}
function Get-PeMachine([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    $reader=New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { return 0 }
        $stream.Position=0x3c
        $offset=$reader.ReadUInt32()
        if ($offset -gt $stream.Length-6) { return 0 }
        $stream.Position=$offset
        if ($reader.ReadUInt32() -ne 0x4550) { return 0 }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose() }
}
function Invoke-ReadOnlyTool([string]$Path,[string]$Arguments='') {
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$Path
    $start.Arguments=$Arguments
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    $start.StandardOutputEncoding=New-Object Text.UTF8Encoding($false)
    $start.StandardErrorEncoding=New-Object Text.UTF8Encoding($false)
    $process=New-Object Diagnostics.Process
    $process.StartInfo=$start
    try {
        [void]$process.Start()
        $stdout=$process.StandardOutput.ReadToEndAsync()
        $stderr=$process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(20000)) {
            $process.Kill()
            throw 'Diagnostic helper timed out.'
        }
        return @{code=$process.ExitCode;stdout=$stdout.Result;stderr=$stderr.Result}
    } finally { $process.Dispose() }
}
$tool=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'diagnostic-info.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$candidates=@()
if (!$GameDirectory) {
    $roots=@((Get-Location).Path,$PSScriptRoot,(Split-Path $PSScriptRoot -Parent))
    if ($env:EU4_GAME_DIR) { $roots+=$env:EU4_GAME_DIR }
    foreach ($key in @('HKCU:\Software\Valve\Steam','HKLM:\Software\WOW6432Node\Valve\Steam')) {
        $steam=Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        $steamRoot=if ($steam.SteamPath) { $steam.SteamPath } else { $steam.InstallPath }
        if (!$steamRoot) { continue }
        $libraries=@($steamRoot)
        $vdf=Join-Path $steamRoot 'steamapps/libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf -PathType Leaf) {
            $raw=Get-Content -LiteralPath $vdf -Raw -Encoding UTF8
            foreach ($match in [regex]::Matches($raw,'"path"\s*"([^"\r\n]+)"')) {
                $libraries+=$match.Groups[1].Value.Replace('\\','\')
            }
        }
        foreach ($library in $libraries) { $roots+=Join-Path $library 'steamapps/common/Europa Universalis IV' }
    }
    $candidates=@($roots | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'eu4.exe') -PathType Leaf } | ForEach-Object { [IO.Path]::GetFullPath($_) } | Sort-Object -Unique)
    if ($candidates.Count -eq 1) { $GameDirectory=$candidates[0] }
}
Add-Check 'host.architecture' $(if ([Environment]::Is64BitOperatingSystem -and [Environment]::Is64BitProcess) {'pass'} else {'fail'}) `
    ([ordered]@{os64=[Environment]::Is64BitOperatingSystem;process64=[Environment]::Is64BitProcess;os_version=[Environment]::OSVersion.VersionString}) 'Use x64 Windows and the 64-bit diagnostic launcher.'
$previousEncoding=[Console]::OutputEncoding
try {
    [Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
    if (!$GameDirectory) {
        Add-Check 'game.directory' 'fail' 'No unique EU4 installation was selected.' 'Run diagnose-windows.cmd -GameDirectory "D:\SteamLibrary\steamapps\common\Europa Universalis IV".'
    } else {
        $GameDirectory=[IO.Path]::GetFullPath($GameDirectory)
        $exe=Join-Path $GameDirectory 'eu4.exe'
        try {
            $machine=Get-PeMachine $exe
            Add-Check 'game.pe' $(if ($machine -eq 0x8664) {'pass'} else {'fail'}) `
                ([ordered]@{path=$exe;machine=$machine;sha256=(Get-Hash $exe)}) 'Select Windows x64 EU4 1.37.5.'
            if ($machine -eq 0x8664) {
                try {
                    $checker=Join-Path $PSScriptRoot 'executable_check.exe'
                    $result=Invoke-ReadOnlyTool $checker ('"'+$exe+'"')
                    Add-Check 'game.instructions' $(if ($result.code -eq 0) {'pass'} else {'fail'}) ($result.stdout+$result.stderr).Trim() 'Review the incompatible instruction in the report; verify the game version and conflicting modifications. The game was not launched.'
                } catch { Add-Check 'game.instructions' 'fail' $_.Exception.Message 'Extract the complete diagnostic archive and retry the read-only instruction checker.' }
            }
        } catch { Add-Check 'game.pe' 'fail' $_.Exception.Message 'Verify the game directory and eu4.exe.' }
        foreach ($file in @(@{name='patch';path='plugins/eu4_unicode_patch.dll';hash=$tool.patch_sha256},@{name='loader';path='VERSION.dll';hash=$tool.loader_sha256})) {
            $path=Join-Path $GameDirectory $file.path
            try {
                $hash=Get-Hash $path
                $machine=Get-PeMachine $path
                Add-Check "patch.$($file.name)" $(if ($machine -ne 0x8664) {'fail'} elseif ($hash -eq $file.hash) {'pass'} else {'warn'}) `
                    ([ordered]@{path=$path;sha256=$hash;expected=$file.hash;machine=$machine}) 'Use an x64 Windows patch and diagnostics from the same release. A hash mismatch alone does not prove incompatibility.'
            } catch { Add-Check "patch.$($file.name)" 'fail' $_.Exception.Message 'Extract the complete Windows main patch package into the directory containing eu4.exe.' }
        }
        $readme=Join-Path $GameDirectory 'EU4UnicodePatch.README.txt'
        $version='Installed package has no version README.'
        if (Test-Path -LiteralPath $readme) {
            $reader=New-Object IO.StreamReader($readme,[Text.Encoding]::UTF8)
            try { $version=$reader.ReadLine() } finally { $reader.Dispose() }
        }
        Add-Check 'patch.version' 'info' $version
        $settings=Join-Path $GameDirectory 'launcher-settings.json'
        try {
            $launcher=Get-Content -LiteralPath $settings -Raw -Encoding UTF8 | ConvertFrom-Json
            $entry=$launcher.exePath
            if (!$entry -or $launcher.gameId -ne 'eu4') { throw 'Invalid launcher game ID or executable entry.' }
            $entryPath=Join-Path $GameDirectory $entry
            Add-Check 'launcher.entry' $(if (Test-Path -LiteralPath $entryPath -PathType Leaf) {'pass'} else {'fail'}) $entry 'Verify the original Paradox Launcher settings through Steam.'
        } catch { Add-Check 'launcher.entry' 'warn' $_.Exception.Message 'Verify the original Paradox Launcher settings through Steam.' }
        $live=@()
        foreach ($process in @(Get-Process eu4 -ErrorAction SilentlyContinue)) {
            try {
                if ($process.Path -ne $exe) { continue }
                $modules=@($process.Modules | Where-Object {$_.ModuleName -match 'eu4_unicode|^VERSION\.dll$|^SDL|icu|dwrite'} | ForEach-Object {$_.FileName})
                $live+=@{pid=$process.Id;libraries=$modules}
            } catch { Add-Check 'runtime.game_access' 'info' $_.Exception.Message }
        }
        Add-Check 'runtime.game_loaded_libraries' $(if ($live.Count) {'pass'} else {'info'}) `
            $(if ($live.Count) {$live} else {'No readable matching game process; game loaded modules are unverified.'})
        $log=Join-Path $GameDirectory 'plugins/eu4_unicode_patch.log'
        if (Test-Path -LiteralPath $log) {
            # Plain .NET strings avoid Get-Content's provider metadata being
            # recursively serialized by Windows PowerShell 5.1 ConvertTo-Json.
            $stream=[IO.File]::Open($log,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
            try {
                $length=[int][Math]::Min(65536,$stream.Length)
                $stream.Position=$stream.Length-$length
                $bytes=New-Object byte[] $length
                $count=$stream.Read($bytes,0,$length)
                $tail=[Text.Encoding]::UTF8.GetString($bytes,0,$count)
            } finally { $stream.Dispose() }
            $selected=@($tail -split '\r?\n' | Where-Object {$_ -match 'Refused|enabled|SHA|missing|atlas'} | Select-Object -Last 12)
            Add-Check 'patch.last_log' 'info' (@{modified=[IO.File]::GetLastWriteTimeUtc($log).ToString('o');lines=$selected}) 'This is an existing log, not a fresh launch result. Review its timestamp.'
        }
    }
    try {
        $probe=Join-Path $PSScriptRoot 'diagnostic_probe.exe'
        $result=Invoke-ReadOnlyTool $probe
        if ($result.code -ne 0) { throw ($result.stdout+$result.stderr) }
        $report=$result.stdout | ConvertFrom-Json
        if (!$report.unicode_services) { throw 'Unicode services probe failed.' }
        Add-Check 'runtime.host_unicode' 'pass' (@{scope='Diagnostic process, not the game';libraries=$report.libraries})
        $missing=@($report.fonts | Where-Object {!$_.covered})
        Add-Check 'fonts.system_coverage' $(if ($missing.Count) {'warn'} else {'pass'}) $report.fonts 'Install the optional font package from the same release, then restart EU4. This probe checks system fonts; mod/private fonts can change game coverage.'
    } catch { Add-Check 'runtime.host_unicode' 'fail' $_.Exception.Message 'Extract the whole Windows diagnostic ZIP and use Windows 10 1903 or newer.' }
} finally { [Console]::OutputEncoding=$previousEncoding }
$output=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($output) | Out-Null
$stem='eu4-diagnostics-windows-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fffffff')
$report=[ordered]@{schema_version=1;generated_at=[DateTime]::UtcNow.ToString('o');tool=$tool;game_directory=$GameDirectory;detected_installations=$candidates;checks=@($checks.ToArray())}
$jsonPath=Join-Path $output ($stem+'.json')
[IO.File]::WriteAllText($jsonPath,($report | ConvertTo-Json -Depth 20)+"`n",(New-Object Text.UTF8Encoding($false)))
$lines=@("EU4 diagnostics $($tool.release_tag)","Game: $GameDirectory")
foreach ($check in $checks) {
    $lines+="[$($check.status.ToUpperInvariant())] $($check.id): $($check.detail | ConvertTo-Json -Compress -Depth 10)"
    if ($check.status -in @('warn','fail') -and $check.remedy) { $lines+='  '+$check.remedy }
}
$lines+="JSON report: $jsonPath"
[IO.File]::WriteAllText((Join-Path $output ($stem+'.txt')),($lines -join "`n")+"`n",(New-Object Text.UTF8Encoding($false)))
$lines | Write-Output
if (@($checks | Where-Object {$_.status -eq 'fail'}).Count) { exit 1 }
exit 0
