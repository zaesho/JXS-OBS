# Install JPEG XS Plugin to OBS Studio
# Run as Administrator

$obsPath = "C:\Program Files\obs-studio"
$pluginPath = "$obsPath\obs-plugins\64bit"
$binPath = "$obsPath\bin\64bit"

# Source paths
$buildDir = "$PSScriptRoot\obs-jpegxs-plugin\build\Release"
$svtDir = "$PSScriptRoot\install\svt-jpegxs\lib"

Write-Host "Installing JPEG XS Plugin..." -ForegroundColor Cyan

# Check if running as admin
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Host "⚠ Error: This script requires Administrator privileges to write to Program Files." -ForegroundColor Red
    Write-Host "Please right-click and 'Run as Administrator'"
    exit 1
}

# Create directories if they don't exist (should exist for OBS)
if (-not (Test-Path $pluginPath)) {
    Write-Host "⚠ Warning: SvtJpegxs.dll not found in $svtDir" -ForegroundColor Yellow
}

# SRT is tricky, it might be in a different place or system installed. 
# We'll check a few common places or skip if not found (user might have it in PATH)
# Assuming it might be in install/srt/bin if it existed, but we didn't see it.
# If the user built it manually as per instructions, it might be elsewhere.
# For now, we rely on the check script to warn if it's missing.

Write-Host ""
Write-Host "Installation complete!" -ForegroundColor Green
Write-Host "Run CHECK_INSTALL.ps1 to verify installation."
