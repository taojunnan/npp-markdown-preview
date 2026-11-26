# 卸载 Markdown 预览插件
[CmdletBinding()]
param(
    [string]$NppDir = 'D:\ProgrameFiles\Notepad++',
    [switch]$KeepConfig
)

$ErrorActionPreference = 'Stop'
$target = Join-Path $NppDir 'plugins\MarkdownPreview'
$config = Join-Path $env:APPDATA 'Notepad++\plugins\config\MarkdownPreview'

if (Get-Process notepad++ -ErrorAction SilentlyContinue) {
    Write-Warning '请先完全退出 Notepad++，否则插件文件可能被占用。'
}

if (Test-Path $target) {
    Remove-Item $target -Recurse -Force
    Write-Host "已删除 $target"
} else {
    Write-Host "未找到 $target"
}

if (-not $KeepConfig -and (Test-Path $config)) {
    Remove-Item $config -Recurse -Force
    Write-Host "已删除配置 $config"
}

Write-Host '卸载完成，重新启动 Notepad++ 生效。'
