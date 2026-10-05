param(
    [Parameter(Mandatory)][ValidateSet('Release','Nightly')][string]$Channel,
    [string]$BuildDate='',
    [string]$BuildDirectory='build',
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
. (Join-Path $PSScriptRoot 'release-common.ps1')
$info=Get-ReleaseInfo $Channel $BuildDate
$repo='YozoraTempest/EU4-Unicode-Patch'

function Get-GitHubObject([string]$Endpoint) {
    $response=& gh api "repos/$repo/$Endpoint" 2>&1
    if ($LASTEXITCODE -ne 0) {
        if (($response -join "`n") -match '\(HTTP 404\)') { return $null }
        throw "GitHub API failed: $response"
    }
    return ($response -join "`n" | ConvertFrom-Json)
}
$tag=Get-GitHubObject "git/ref/tags/$($info.Tag)"
if ($tag) {
    $object=$tag.object
    while ($object.type -eq 'tag') { $object=(Get-GitHubObject "git/tags/$($object.sha)").object }
    if ($object.type -ne 'commit' -or $object.sha -ne $info.SourceCommit) { throw 'Existing tag points to a different source commit.' }
}
$release=Get-GitHubObject "releases/tags/$($info.Tag)"
if ($release -and !$release.draft) {
    $expectedAssets=@($info.PlayerPackage,$info.FontPackage,'SHA256SUMS.txt') | Sort-Object
    if (!$tag -or [bool]$release.prerelease -ne ($Channel -eq 'Nightly') -or
        (@($release.assets.name | Sort-Object) -join ',') -cne ($expectedAssets -join ',')) {
        throw 'Existing published release does not match the expected channel or assets.'
    }
    if ($env:GITHUB_OUTPUT) { "skip=true" | Add-Content -LiteralPath $env:GITHUB_OUTPUT }
    Write-Output "Already published: $($release.html_url)"
    exit 0
}
if ($Channel -eq 'Release') {
    $latest=Get-GitHubObject 'releases/latest'
    if ($latest -and $latest.tag_name -match '^v([0-9]+\.[0-9]+\.[0-9]+)$' -and
        [version]$info.Version -le [version]$Matches[1]) { throw 'Formal release version must increase.' }
}
if ($env:GITHUB_OUTPUT) { "skip=false" | Add-Content -LiteralPath $env:GITHUB_OUTPUT }
if ($CheckOnly) { exit 0 }

Assert-CleanCheckout
$root=Get-BuildRoot $BuildDirectory
$package=Get-Content -LiteralPath (Join-Path $root 'package-info.json') -Raw | ConvertFrom-Json
if ($package.Tag -ne $info.Tag -or $package.SourceCommit -ne $info.SourceCommit) { throw 'Package does not match this release.' }
& (Join-Path $PSScriptRoot 'test-package.ps1') -BuildDirectory $BuildDirectory
$updates=''
if ($Channel -eq 'Release') {
    $changelog=[IO.File]::ReadAllText((Join-Path $releaseProjectRoot 'CHANGELOG.md'))
    $section=[regex]::Match($changelog,'(?ms)^## '+[regex]::Escape($info.Version)+'\s*\r?\n(.*?)(?=^## |\z)')
    if (!$section.Success) { throw 'Add this version to CHANGELOG.md before releasing.' }
    $updates=$section.Groups[1].Value.Trim()+"`n`n"
}
$channelNote=if ($Channel -eq 'Nightly') { "Nightly 测试版，构建日期：$($info.BuildDate)（北京时间）。`n`n" } else { '' }
$notes=$channelNote+$updates+@"
适用：EU4 1.37.5.0 Inca / Windows x64，Windows 10 1903 或更新版本。

安装：退出游戏，将主包全部内容解压到 eu4.exe 所在目录并覆盖。需要补字时再安装可选字体包；补丁优先使用系统字体。

限制：UI 与地图图集均有内存上限；模组自带位图字体使用原字库；复杂文字光标和选区适配限于单行编辑框。

提交：[$($info.SourceCommit.Substring(0,7))](https://github.com/$repo/commit/$($info.SourceCommit))

作者：VulonLok · [QQ 交流群](https://qm.qq.com/q/Csnqqd8rUO)
"@
$notesFile=Join-Path $root 'release-notes.md'
[IO.File]::WriteAllText($notesFile,$notes,[Text.UTF8Encoding]::new($false))
if (!$release) {
    $arguments=@('release','create',$info.Tag,'--repo',$repo,'--target',$info.SourceCommit,'--draft','--title',"EU4 Unicode Patch $($info.Tag)",'--notes-file',$notesFile,'--latest=false')
    if ($Channel -eq 'Nightly') { $arguments+='--prerelease' }
    & gh @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create draft release.' }
} else {
    & gh release edit $info.Tag --repo $repo --notes-file $notesFile
    if ($LASTEXITCODE -ne 0) { throw 'Cannot update draft release notes.' }
}
$dist=Join-Path $releaseProjectRoot 'dist'
$assets=@(@($info.PlayerPackage,$info.FontPackage,'SHA256SUMS.txt') | ForEach-Object { Join-Path $dist $_ })
& gh release upload $info.Tag @assets --repo $repo --clobber
if ($LASTEXITCODE -ne 0) { throw 'Draft release asset upload failed.' }
$arguments=@('release','edit',$info.Tag,'--repo',$repo,'--draft=false')
if ($Channel -eq 'Nightly') { $arguments+=@('--prerelease','--latest=false') } else { $arguments+=@('--prerelease=false','--latest') }
& gh @arguments
if ($LASTEXITCODE -ne 0) { throw 'Cannot publish release.' }
$url="https://github.com/$repo/releases/tag/$($info.Tag)"
if ($env:GITHUB_STEP_SUMMARY) { "[$($info.Tag)]($url)" | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
Write-Output $url
