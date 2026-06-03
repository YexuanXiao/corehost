# ── Set-CorehostDefaultTerminal.ps1 ──────────────────────────
# 将 corehost 注册为系统默认终端，或还原回之前的设置。
#
# 用法:
#   .\scripts\Set-CorehostDefaultTerminal.ps1 -Install
#   .\scripts\Set-CorehostDefaultTerminal.ps1 -Uninstall
#
# 原理:
#   Install 备份当前 DelegationConsole 值，然后设为 corehost
#   CLSID，并注册 COM 服务器。DelegationTerminal 不受影响。
#
#   Uninstall 从备份恢复 DelegationConsole（无备份则设为 WT
#   稳定版），移除 corehost 的 COM 服务器注册。
#
# 注意:
#   - 不需要管理员权限（所有操作仅在 HKCU 下）
#   - 只影响当前用户
#   - 修改后已运行的 conhost 不受影响，新启动的控制台生效

param(
    [Parameter(Mandatory = $true, ParameterSetName = "Install")]
    [switch]$Install,
    [Parameter(Mandatory = $true, ParameterSetName = "Uninstall")]
    [switch]$Uninstall,
    [Parameter(ParameterSetName = "Install")]
    [string]$CorehostPath = ""
)

# ── 常量 ─────────────────────────────────────────────────────
$CoreClsid = "{47A3A1A0-2D3C-4F5E-8B1A-9C3D4E5F6A7B}"
$WtConsoleClsid = "{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBEE9}"

$RegPath = "HKCU:\Console\%%Startup"
$ClsidRoot = "HKCU:\Software\Classes\CLSID\$CoreClsid"
$ClsidLocalServer = "$ClsidRoot\LocalServer32"

function Write-Step {
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] $($args[0])" -ForegroundColor Cyan
}

# ── Install ──────────────────────────────────────────────────
function Install {
    # 确定 corehost.exe 路径
    if (-not $CorehostPath) {
        $scriptDir = Split-Path -Parent $PSScriptRoot
        $candidates = @(
            Join-Path $scriptDir "build\Release\corehost.exe"
            Join-Path $scriptDir "build\corehost.exe"
        )
        $CorehostPath = ($candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)
        if (-not $CorehostPath) {
            Write-Host "错误: 找不到 corehost.exe，请通过 -CorehostPath 参数指定路径。" -ForegroundColor Red
            exit 1
        }
    }
    if (-not (Test-Path $CorehostPath)) {
        Write-Host "错误: 路径不存在: $CorehostPath" -ForegroundColor Red
        exit 1
    }
    $CorehostPath = (Resolve-Path $CorehostPath).Path
    Write-Step "corehost.exe 路径: $CorehostPath"

    # 确保 %%Startup 键存在
    if (-not (Test-Path $RegPath)) {
        New-Item -Path $RegPath -Force | Out-Null
        Write-Step "创建注册表键: $RegPath"
    }

    # 备份当前 DelegationConsole（如果存在且不是 corehost 自身）
    $current = $null
    try { $current = Get-ItemPropertyValue -Path $RegPath -Name DelegationConsole -ErrorAction Stop } catch {}
    if ($current -and $current -ne $CoreClsid) {
        Set-ItemProperty -Path $RegPath -Name "DelegationConsoleBackup" -Value $current
        Write-Step "备份原 DelegationConsole → $current"
    } elseif (-not $current) {
        Set-ItemProperty -Path $RegPath -Name "DelegationConsoleBackup" -Value ""
        Write-Step "备份原 DelegationConsole → (无)"
    } else {
        Write-Step "DelegationConsole 已经是 corehost，跳过备份"
    }

    # 设置 DelegationConsole = corehost CLSID
    Set-ItemProperty -Path $RegPath -Name "DelegationConsole" -Value $CoreClsid
    Write-Step "DelegationConsole → $CoreClsid (corehost)"

    # 不修改 DelegationTerminal——保留用户原有的终端选择

    # 注册 COM 服务器 (HKCU)
    if (-not (Test-Path $ClsidRoot)) {
        New-Item -Path $ClsidRoot -Force | Out-Null
    }
    Set-ItemProperty -Path $ClsidRoot -Name "(Default)" -Value "Corehost Console Handoff Server"

    if (-not (Test-Path $ClsidLocalServer)) {
        New-Item -Path $ClsidLocalServer -Force | Out-Null
    }
    Set-ItemProperty -Path $ClsidLocalServer -Name "(Default)" -Value "$CorehostPath"

    Write-Step "COM 服务器注册完成 → HKCU\Software\Classes\CLSID\$CoreClsid"

    Write-Host ""
    Write-Host "✔ 安装完成。" -ForegroundColor Green
    Write-Host "下次启动控制台程序时将使用 corehost 作为默认终端。" -ForegroundColor Green
    Write-Host "还原: .\scripts\Set-CorehostDefaultTerminal.ps1 -UnInstall" -ForegroundColor Gray
}

# ── Uninstall ────────────────────────────────────────────────
function Uninstall {
    if (Test-Path $RegPath) {
        # 从备份恢复 DelegationConsole
        $backup = $null
        try { $backup = Get-ItemPropertyValue -Path $RegPath -Name DelegationConsoleBackup -ErrorAction Stop } catch {}

        if ($null -ne $backup -and $backup -ne "") {
            # 恢复备份的原始值
            Set-ItemProperty -Path $RegPath -Name "DelegationConsole" -Value $backup
            Write-Step "DelegationConsole 恢复 → $backup"
        } elseif ($null -ne $backup -and $backup -eq "") {
            # 安装前 DelegationConsole 不存在，移除该值
            Remove-ItemProperty -Path $RegPath -Name DelegationConsole -ErrorAction SilentlyContinue
            Write-Step "DelegationConsole 已移除（安装前不存在）"
        } else {
            # 无备份（直接跑 Uninstall），回退到 WT 稳定版
            Set-ItemProperty -Path $RegPath -Name "DelegationConsole" -Value $WtConsoleClsid
            Write-Step "DelegationConsole → $WtConsoleClsid (WT 稳定版，无备份)"
        }

        # 清理备份值
        Remove-ItemProperty -Path $RegPath -Name DelegationConsoleBackup -ErrorAction SilentlyContinue
    } else {
        Write-Step "注册表键不存在，跳过"
    }

    # 不移除 DelegationTerminal——它从未被 Install 修改过

    # 移除 corehost 的 CLSID 注册
    if (Test-Path $ClsidRoot) {
        Remove-Item -Path $ClsidRoot -Recurse -Force
        Write-Step "移除 COM 服务器注册: HKCU\Software\Classes\CLSID\$CoreClsid"
    } else {
        Write-Step "COM 服务器未注册，跳过"
    }

    Write-Host ""
    Write-Host "✔ 卸载完成。" -ForegroundColor Green
    Write-Host "默认终端已还原。" -ForegroundColor Green
}

# ── 入口 ─────────────────────────────────────────────────────
if ($Install) {
    Install
} elseif ($Uninstall) {
    Uninstall
}
