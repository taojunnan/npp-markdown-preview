# 在 Notepad++ 退出后完成收尾：
#   1) 安装最新的 MarkdownPreview 插件
#   2) 删除旧的 MarkdownViewerPlusPlus 插件
# 由安装流程自动在后台启动，也可以手动运行。
[CmdletBinding()]
param(
    [string]$NppDir = 'D:\ProgrameFiles\Notepad++',
    [string]$Dist   = '',
    [int]$WaitSeconds = 3600
)

if (-not $Dist) { $Dist = Join-Path $PSScriptRoot 'dist\MarkdownPreview' }

$log = Join-Path $env:TEMP 'npp-md-setup.log'
function Write-Log([string]$msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss'), $msg
    Add-Content -Path $log -Value $line -Encoding UTF8
}

Write-Log "等待 Notepad++ 退出…"
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process notepad++ -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Seconds 1
}
if (Get-Process notepad++ -ErrorAction SilentlyContinue) {
    Write-Log '超时：Notepad++ 仍在运行，未做任何修改。'
    exit 1
}

Start-Sleep -Seconds 1
$target = Join-Path $NppDir 'plugins\MarkdownPreview'
Write-Log "安装插件到 $target"
try {
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Get-ChildItem $target -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $Dist '*') $target -Recurse -Force
    Write-Log '插件文件已更新。'
} catch {
    Write-Log "安装失败：$($_.Exception.Message)"
    exit 2
}

$old = Join-Path $NppDir 'plugins\MarkdownViewerPlusPlus'
if (Test-Path $old) {
    try {
        Remove-Item $old -Recurse -Force
        Write-Log "已删除旧插件 $old"
    } catch {
        Write-Log "删除旧插件失败：$($_.Exception.Message)"
    }
}

$oldCfg = Join-Path $env:APPDATA 'Notepad++\plugins\config\MarkdownViewerPlusPlus'
if (Test-Path $oldCfg) {
    try {
        Remove-Item $oldCfg -Recurse -Force
        Write-Log "已删除旧插件配置 $oldCfg"
    } catch {
        Write-Log "删除旧插件配置失败：$($_.Exception.Message)"
    }
}

# 预览面板默认关闭：只在用户点工具栏按钮/菜单时打开
$cfgDir = Join-Path $env:APPDATA 'Notepad++\plugins\config\MarkdownPreview'
$ini = Join-Path $cfgDir 'MarkdownPreview.ini'
try {
    if (Test-Path $ini) {
        $lines = Get-Content $ini
        if ($lines -match '^autoOpen=') {
            $lines = $lines -replace '^autoOpen=\d', 'autoOpen=0'
        } else {
            $lines = @('[general]', 'autoOpen=0') + $lines
        }
        Set-Content -Path $ini -Value $lines -Encoding UTF8
    } else {
        New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
        Set-Content -Path $ini -Value @('[general]', 'autoOpen=0') -Encoding UTF8
    }
    Write-Log '已设置 autoOpen=0（默认不自动打开）'
} catch {
    Write-Log "写入配置失败：$($_.Exception.Message)"
}

Write-Log '完成，重新打开 Notepad++ 即可。'
