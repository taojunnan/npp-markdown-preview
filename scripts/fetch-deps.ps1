<#
    Markdown Preview for Notepad++ — 拉取构建依赖

    把构建需要的东西下载到底盘上的 _build 目录（与项目目录同级）：

        <parent>\_build\webview2\pkg\build\native\...   WebView2 SDK（NuGet 包）
        <parent>\_build\js\<alias>\package\...          前端库（npm 包）

    只需要联网跑一次，build.ps1 之后就直接用这些文件。
#>
[CmdletBinding()]
param(
    [string]$Dest = '',
    [string]$WebView2Version = '1.0.4392-prerelease'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# build.ps1 looks for _build next to the repository, i.e. the parent of the
# folder this script lives in.  Resolve it here (not in the param block, where
# $PSScriptRoot is not set yet on Windows PowerShell 5.1).
if (-not $Dest) {
    $repoRoot = Split-Path $PSScriptRoot -Parent
    $Dest = Join-Path (Split-Path $repoRoot -Parent) '_build'
}
Write-Host "依赖目录：$Dest" -ForegroundColor DarkGray

New-Item -ItemType Directory -Force -Path $Dest | Out-Null

# ---------------------------------------------------------------- WebView2 SDK
$pkgDir = Join-Path $Dest 'webview2\pkg'
$header = Join-Path $pkgDir 'build\native\include\WebView2.h'
if (Test-Path $header) {
    Write-Host '== WebView2 SDK 已存在，跳过 ==' -ForegroundColor Cyan
} else {
    Write-Host "== WebView2 SDK $WebView2Version ==" -ForegroundColor Cyan
    $nupkg = Join-Path $Dest 'webview2\wv2.nupkg'
    New-Item -ItemType Directory -Force -Path (Split-Path $nupkg -Parent) | Out-Null
    Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$WebView2Version" -OutFile $nupkg
    if (Test-Path $pkgDir) { Remove-Item $pkgDir -Recurse -Force }
    # Expand-Archive refuses non-.zip extensions, and a .nupkg is just a zip.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($nupkg, $pkgDir)
    if (-not (Test-Path $header)) { throw 'WebView2 SDK 解压后缺少 include\WebView2.h' }
}

# ------------------------------------------------------------------ 前端库
$jsDir = Join-Path $Dest 'js'
New-Item -ItemType Directory -Force -Path $jsDir | Out-Null

$packages = @(
    @{ Alias = 'marked';                  Spec = 'marked@18.3.0' },
    @{ Alias = 'dompurify';               Spec = 'dompurify@3.5.0' },
    @{ Alias = '_highlightjs_cdn-assets'; Spec = '@highlightjs/cdn-assets@11.12.0' },
    @{ Alias = 'marked-katex-extension';  Spec = 'marked-katex-extension@5.1.13' },
    @{ Alias = 'katex16';                 Spec = 'katex@0.16.47' },
    @{ Alias = 'github-markdown-css';     Spec = 'github-markdown-css@5.9.0' }
)

foreach ($p in $packages) {
    $target = Join-Path $jsDir $p.Alias
    if (Test-Path (Join-Path $target 'package')) {
        Write-Host "   $($p.Alias) 已存在" -ForegroundColor DarkGray
        continue
    }
    Write-Host "   下载 $($p.Spec)"
    $tmp = Join-Path $env:TEMP ("nppmd-pack-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    & npm pack $p.Spec --pack-destination $tmp --silent | Out-Null
    $tgz = Get-ChildItem $tmp -Filter '*.tgz' | Select-Object -First 1
    if (-not $tgz) { throw "npm pack 失败：$($p.Spec)" }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    & tar -xzf $tgz.FullName -C $target
    Copy-Item $tgz.FullName (Join-Path $jsDir ($p.Alias + '.tgz')) -Force
    Remove-Item $tmp -Recurse -Force
}

Write-Host '依赖就绪，可以运行 build.ps1。' -ForegroundColor Green
