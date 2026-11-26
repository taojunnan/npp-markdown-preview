# =============================================================================
#  Markdown Preview for Notepad++ — build & install
#  Toolchain: Zig (bundled clang + MinGW-w64 headers), x64 only.
# =============================================================================
[CmdletBinding()]
param(
    [string]$NppDir = 'D:\ProgrameFiles\Notepad++',
    [string]$Zig = 'C:\Users\taojn6\AppData\Local\Programs\Python\Python311\Lib\site-packages\ziglang\zig.exe',
    [switch]$NoInstall
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dist = Join-Path $root 'dist\MarkdownPreview'
$webOut = Join-Path $dist 'web'
$vendorOut = Join-Path $webOut 'vendor'

# Vendored third-party sources (downloaded once into _build)
$build = Join-Path (Split-Path $root -Parent) '_build'
$libs = Join-Path $build 'js'
$wv2 = Join-Path $build 'webview2\pkg\build\native'

function Need($path, $what) {
    if (-not (Test-Path $path)) { throw "缺少$what：$path" }
}
Need $Zig 'Zig 编译器（可修改 -Zig 参数）'
Need (Join-Path $wv2 'include\WebView2.h') 'WebView2 SDK 头文件'
Need (Join-Path $wv2 'x64\WebView2Loader.dll') 'WebView2Loader.dll'
Need (Join-Path $libs 'marked\package\lib\marked.umd.js') 'marked 渲染库'

Write-Host '== 1/4 收集前端资源 ==' -ForegroundColor Cyan
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $vendorOut, (Join-Path $vendorOut 'fonts') | Out-Null

Copy-Item (Join-Path $root 'web\index.html') $webOut
Copy-Item (Join-Path $root 'web\style.css') $webOut
Copy-Item (Join-Path $root 'web\app.js') $webOut

Copy-Item (Join-Path $libs 'marked\package\lib\marked.umd.js') (Join-Path $vendorOut 'marked.umd.js')
Copy-Item (Join-Path $libs 'dompurify\package\dist\purify.min.js') (Join-Path $vendorOut 'purify.min.js')
Copy-Item (Join-Path $libs '_highlightjs_cdn-assets\package\highlight.min.js') (Join-Path $vendorOut 'highlight.min.js')
Copy-Item (Join-Path $libs 'marked-katex-extension\package\lib\index.umd.js') (Join-Path $vendorOut 'marked-katex.umd.js')
Copy-Item (Join-Path $libs 'katex16\package\dist\katex.min.js') (Join-Path $vendorOut 'katex.min.js')
Copy-Item (Join-Path $libs 'katex16\package\dist\katex.min.css') (Join-Path $vendorOut 'katex.min.css')
Copy-Item (Join-Path $libs 'katex16\package\dist\fonts\*') (Join-Path $vendorOut 'fonts')

Copy-Item (Join-Path $libs 'github-markdown-css\package\github-markdown-light.css') (Join-Path $vendorOut 'gh-light.css')
Copy-Item (Join-Path $libs 'github-markdown-css\package\github-markdown-dark.css') (Join-Path $vendorOut 'gh-dark.css')
Copy-Item (Join-Path $libs '_highlightjs_cdn-assets\package\styles\github.min.css') (Join-Path $vendorOut 'hljs-light.css')
Copy-Item (Join-Path $libs '_highlightjs_cdn-assets\package\styles\github-dark.min.css') (Join-Path $vendorOut 'hljs-dark.css')

# cache-busting token so WebView2 never serves a stale app.js
$hashInput = (Get-Content (Join-Path $webOut 'index.html') -Raw) + (Get-Content (Join-Path $webOut 'app.js') -Raw) +
             (Get-Content (Join-Path $webOut 'style.css') -Raw)
$sha = [System.Security.Cryptography.SHA1]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($hashInput))
$ver = ([BitConverter]::ToString($sha) -replace '-', '').Substring(0, 10).ToLower()
$indexPath = Join-Path $webOut 'index.html'
# NOTE: read/write as explicit UTF-8. PowerShell 5.1's Get-Content/Set-Content default to the
# ANSI code page and would corrupt Chinese text and any non-ASCII punctuation in the markup.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$html = [IO.File]::ReadAllText($indexPath, $utf8NoBom)
[IO.File]::WriteAllText($indexPath, $html.Replace('{{V}}', $ver), $utf8NoBom)
Write-Host "   资源版本 $ver"

Write-Host '== 2/4 编译插件 DLL ==' -ForegroundColor Cyan
$dll = Join-Path $dist 'MarkdownPreview.dll'
$ErrorActionPreference = 'Continue'
$zigLog = & $Zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -shared `
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN `
    -I (Join-Path $root 'src') -I (Join-Path $wv2 'include') `
    (Join-Path $root 'src\plugin.cpp') (Join-Path $root 'src\webview.cpp') `
    -o $dll `
    -lole32 -loleaut32 -luuid -luser32 -lgdi32 -lshell32 -lcomdlg32 -lshlwapi -ladvapi32 -lcomctl32 2>&1
$zigCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($zigCode -ne 0) {
    $zigLog | ForEach-Object { Write-Host $_ }
    throw 'DLL 编译失败'
}
$zigLog | Where-Object { $_ -match 'error|warning' } | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkYellow }
if (-not (Test-Path $dll)) { throw 'DLL 编译失败：未生成文件' }
Write-Host ("   MarkdownPreview.dll  {0:N0} KB" -f ((Get-Item $dll).Length / 1KB))

Copy-Item (Join-Path $wv2 'x64\WebView2Loader.dll') $dist
Remove-Item (Join-Path $dist '*.pdb'), (Join-Path $dist '*.lib') -Force -ErrorAction SilentlyContinue

Write-Host '== 3/4 校验插件能否被加载 ==' -ForegroundColor Cyan
try {
    $probeExe = Join-Path $env:TEMP 'npp_md_loadtest.exe'
    $ErrorActionPreference = 'Continue'
    & $Zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -o $probeExe (Join-Path $root 'test\loadtest.cpp') 2>&1 | Out-Null
    $ErrorActionPreference = 'Stop'
    if (Test-Path $probeExe) {
        $probeOut = & $probeExe $dll 2>&1
        if ($LASTEXITCODE -ne 0) { throw ($probeOut -join ' ') }
        Write-Host '   通过：getName / getFuncsArray / isUnicode 均可调用'
        Remove-Item $probeExe -Force -ErrorAction SilentlyContinue
    } else {
        Write-Warning '跳过加载自检（探测程序编译失败）'
    }
} catch {
    Write-Warning "加载自检未通过：$($_.Exception.Message)"
}

Write-Host '== 4/4 安装到 Notepad++ ==' -ForegroundColor Cyan
if ($NoInstall) {
    Write-Host "   已跳过安装，产物位于 $dist"
    return
}

$target = Join-Path $NppDir 'plugins\MarkdownPreview'
$nppExe = Join-Path $NppDir 'notepad++.exe'
Need $nppExe 'Notepad++ 主程序'
if (Get-Process notepad++ -ErrorAction SilentlyContinue) {
    Write-Warning 'Notepad++ 正在运行；若 DLL 被占用，请关闭后重新运行本脚本。'
}
New-Item -ItemType Directory -Force -Path $target | Out-Null
# remove stale files from previous versions (but keep the folder itself)
Get-ChildItem $target -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $dist '*') $target -Recurse -Force
Write-Host "   已安装到 $target"
Write-Host ''
Write-Host '完成。重启 Notepad++ 后，在「插件」菜单中打开 Markdown 预览。' -ForegroundColor Green
