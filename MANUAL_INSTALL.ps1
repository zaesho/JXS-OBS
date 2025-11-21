# Simple manual install script
# Run this as Administrator

$ErrorActionPreference = "Stop"

Write-Host "Stopping OBS if running..." -ForegroundColor Yellow
Get-Process obs64 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

Write-Host "Installing encoder plugin..." -ForegroundColor Cyan
Copy-Item "c:\Users\niost\OneDrive\Desktop\JXS-OBS\obs-jpegxs-plugin\build\Release\obs-jpegxs-output.dll" `
          "C:\Program Files\obs-studio\obs-plugins\64bit\obs-jpegxs-output.dll" -Force

Write-Host "Installing decoder plugin..." -ForegroundColor Cyan
Copy-Item "c:\Users\niost\OneDrive\Desktop\JXS-OBS\obs-jpegxs-plugin\build\Release\obs-jpegxs-input.dll" `
          "C:\Program Files\obs-studio\obs-plugins\64bit\obs-jpegxs-input.dll" -Force

Write-Host ""
Write-Host "Installation complete!" -ForegroundColor Green
Write-Host "You can now restart OBS." -ForegroundColor Green
