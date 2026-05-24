# test_auto.ps1 - Start-Process powershell -> wait -> kill corehost -> analyze logs
# Usage: powershell -ExecutionPolicy Bypass -File test_auto.ps1

$ErrorActionPreference = "Stop"
$scriptRoot = $PSScriptRoot
$logDir = "$scriptRoot\build\logs"
if (-not (Test-Path $logDir)) { $logDir = "$scriptRoot\build\Release\logs" }

# 1. Clean old logs
Write-Host "=== [1/3] Clean old logs ===" -ForegroundColor Cyan
if (Test-Path $logDir) {
    Get-ChildItem "$logDir\corehost_*.log" -ErrorAction SilentlyContinue | Remove-Item -Force
}

# 2. Start-Process powershell, wait, kill corehost
Write-Host "=== [2/3] Start-Process powershell ===" -ForegroundColor Cyan
$ps = Start-Process powershell -PassThru
Write-Host "  powershell PID=$($ps.Id)"
Start-Sleep -Seconds 8
$killed = Get-Process corehost -ErrorAction SilentlyContinue
if ($killed) {
    $killed | ForEach-Object { Write-Host "  Killing corehost PID=$($_.Id)"; Stop-Process -Id $_.Id -Force }
}
if (-not $ps.HasExited) { Stop-Process -Id $ps.Id -Force }
Start-Sleep -Seconds 2

# 3. Analyze logs
Write-Host "=== [3/3] Analyze logs ===" -ForegroundColor Cyan
$logs = @(Get-ChildItem "$logDir\corehost_*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
if ($logs.Count -eq 0) { Write-Host "No log files" -ForegroundColor Red; exit 1 }
$log = $logs[0]
Write-Host "  Log: $($log.Name) ($($log.Length) bytes)"

# Count key events
$rawWr  = (Select-String -Path $log.FullName -Pattern "RAW_WRITE writing" -SimpleMatch).Count
$rawIn  = (Select-String -Path $log.FullName -Pattern "[bridge] IN " -SimpleMatch).Count
$rawOut = (Select-String -Path $log.FullName -Pattern "OUT flush" -SimpleMatch).Count
$ciRead = (Select-String -Path $log.FullName -Pattern "CI R n=[1-9]" -SimpleMatch).Count
$ciPeek = (Select-String -Path $log.FullName -Pattern "CI P n=[1-9]" -SimpleMatch).Count
$testInj = (Select-String -Path $log.FullName -Pattern "TEST inject" -SimpleMatch).Count

Write-Host "  RAW_WRITE : $rawWr"
Write-Host "  RAW_IN    : $rawIn"
Write-Host "  RAW_OUT   : $rawOut"
Write-Host "  CI_PEEK   : $ciPeek"
Write-Host "  CI_READ   : $ciRead"
Write-Host "  TEST_inj  : $testInj"

Write-Host "`n=== Done ===" -ForegroundColor Cyan
