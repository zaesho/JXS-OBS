# Force Install JPEG XS Plugin
# Run as Administrator

$ErrorActionPreference = "Stop"
$obsPath = "C:\Program Files\obs-studio"
$pluginPath = "$obsPath\obs-plugins\64bit"
$sourceDll = "$PSScriptRoot\obs-jpegxs-plugin\build\Release\obs-jpegxs-output.dll"
$destDll = "$pluginPath\obs-jpegxs-output.dll"

# 1. Check Admin
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Host "Requesting Administrator privileges..." -ForegroundColor Yellow
    Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

Write-Host "--- JPEG XS Plugin Force Installer ---" -ForegroundColor Cyan

# 2. Kill OBS
Write-Host "Checking for running OBS processes..."
$obsProcess = Get-Process obs64 -ErrorAction SilentlyContinue
if ($obsProcess) {
    Write-Host "Stopping OBS Studio..." -ForegroundColor Yellow
    Stop-Process -Name obs64 -Force
    Start-Sleep -Seconds 2
}

# 3. Verify Source
if (-not (Test-Path $sourceDll)) {
    Write-Host "ERROR: Source DLL not found at:" -ForegroundColor Red
    Write-Host $sourceDll
    Read-Host "Press Enter to exit..."
    exit 1
}

# 4. Remove Old File
if (Test-Path $destDll) {
    Write-Host "Removing old plugin..."
    try {
        Remove-Item $destDll -Force
    } catch {
        Write-Host "ERROR: Could not remove old file. Is it still locked?" -ForegroundColor Red
        Write-Host $_
        Read-Host "Press Enter to exit..."
        exit 1
    }
}

# 5. Copy New File
Write-Host "Copying new plugin..."
try {
    Copy-Item $sourceDll -Destination $destDll -Force
    Write-Host "Success!" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Copy failed." -ForegroundColor Red
    Write-Host $_
    Read-Host "Press Enter to exit..."
    exit 1
}

# 6. Verify
$newFile = Get-Item $destDll
Write-Host "Installed File Info:"
Write-Host "  Time: $($newFile.LastWriteTime)"
Write-Host "  Size: $($newFile.Length)"

Write-Host ""
Write-Host "Installation Complete! You can now start OBS." -ForegroundColor Green
Read-Host "Press Enter to close..."
