# JPEG XS Plugin Diagnostic Script
# Checks installation status and opens OBS log for verification

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " JPEG XS Plugin Installation Check" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$obsPath = "C:\Program Files\obs-studio"
$pluginPath = "$obsPath\obs-plugins\64bit"
$binPath = "$obsPath\bin\64bit"
$allOk = $true

# Check if OBS is installed
if (-not (Test-Path $obsPath)) {
    Write-Host "✗ OBS Studio not found at: $obsPath" -ForegroundColor Red
    Write-Host "  Please install OBS Studio first" -ForegroundColor Yellow
    exit 1
}

Write-Host "✓ OBS Studio found at: $obsPath" -ForegroundColor Green
Write-Host ""

# Check encoder plugin
Write-Host "Checking plugins..." -ForegroundColor Cyan
if (Test-Path "$pluginPath\obs-jpegxs-output.dll") {
    $size = (Get-Item "$pluginPath\obs-jpegxs-output.dll").Length / 1KB
    Write-Host "✓ Encoder plugin: obs-jpegxs-output.dll ($([math]::Round($size, 2)) KB)" -ForegroundColor Green
} else {
    Write-Host "✗ Encoder plugin NOT found: $pluginPath\obs-jpegxs-output.dll" -ForegroundColor Red
    $allOk = $false
}

# Check decoder plugin
if (Test-Path "$pluginPath\obs-jpegxs-input.dll") {
    $size = (Get-Item "$pluginPath\obs-jpegxs-input.dll").Length / 1KB
    Write-Host "✓ Decoder plugin: obs-jpegxs-input.dll ($([math]::Round($size, 2)) KB)" -ForegroundColor Green
} else {
    Write-Host "✗ Decoder plugin NOT found: $pluginPath\obs-jpegxs-input.dll" -ForegroundColor Red
    $allOk = $false
}

Write-Host ""
Write-Host "Checking dependencies..." -ForegroundColor Cyan

# Check SVT-JPEG-XS library
if (Test-Path "$binPath\SvtJpegxs.dll") {
    $size = (Get-Item "$binPath\SvtJpegxs.dll").Length / 1KB
    Write-Host "✓ SVT-JPEG-XS: SvtJpegxs.dll ($([math]::Round($size, 2)) KB)" -ForegroundColor Green
} else {
    Write-Host "✗ SVT-JPEG-XS library NOT found: $binPath\SvtJpegxs.dll" -ForegroundColor Red
    Write-Host "  This is REQUIRED for the plugin to load!" -ForegroundColor Yellow
    $allOk = $false
}

# Check SRT library
if (Test-Path "$binPath\srt.dll") {
    Write-Host "✓ SRT library: srt.dll" -ForegroundColor Green
} else {
    Write-Host "⚠ SRT library not found: $binPath\srt.dll" -ForegroundColor Yellow
    Write-Host "  May be found in system PATH, or plugin will fail at runtime" -ForegroundColor Yellow
}

# Check OpenSSL
if (Test-Path "$binPath\libcrypto-3-x64.dll") {
    Write-Host "✓ OpenSSL: libcrypto-3-x64.dll" -ForegroundColor Green
} else {
    Write-Host "⚠ OpenSSL library not found: $binPath\libcrypto-3-x64.dll" -ForegroundColor Yellow
}

if (Test-Path "$binPath\libssl-3-x64.dll") {
    Write-Host "✓ OpenSSL: libssl-3-x64.dll" -ForegroundColor Green
} else {
    Write-Host "⚠ OpenSSL library not found: $binPath\libssl-3-x64.dll" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan

if (-not $allOk) {
    Write-Host ""
    Write-Host "⚠ INSTALLATION INCOMPLETE" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To fix this, run the installer as Administrator:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  1. Right-click PowerShell → Run as Administrator" -ForegroundColor White
    Write-Host "  2. cd `"c:\Users\niost\OneDrive\Desktop\JXS-OBS`"" -ForegroundColor White
    Write-Host "  3. .\INSTALL_TO_OBS.ps1" -ForegroundColor White
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "✓ ALL FILES INSTALLED CORRECTLY!" -ForegroundColor Green
    Write-Host ""
}

# Check if OBS is running
$obsProcess = Get-Process obs64 -ErrorAction SilentlyContinue
if ($obsProcess) {
    Write-Host "ℹ OBS Studio is currently running (PID: $($obsProcess.Id))" -ForegroundColor Cyan
    Write-Host "  You may need to restart OBS to load new plugins" -ForegroundColor Cyan
    Write-Host ""
}

# Find and display the latest OBS log
$logPath = "$env:APPDATA\obs-studio\logs"
if (Test-Path $logPath) {
    $latestLog = Get-ChildItem $logPath -Filter "*.txt" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    
    if ($latestLog) {
        Write-Host "Opening latest OBS log file..." -ForegroundColor Cyan
        Write-Host "  $($latestLog.FullName)" -ForegroundColor Gray
        Write-Host ""
        Write-Host "Look for these lines in the log:" -ForegroundColor Yellow
        Write-Host "  - [JPEG XS] Plugin version 0.1.0 loading..." -ForegroundColor Gray
        Write-Host "  - [JPEG XS] JPEG XS encoder output registered successfully" -ForegroundColor Gray
        Write-Host "  - [obs-jpegxs-input] JPEG XS decoder source registered successfully" -ForegroundColor Gray
        Write-Host ""
        Write-Host "If you DON'T see these, the plugins didn't load (check dependencies above)." -ForegroundColor Yellow
        Write-Host ""
        
        Start-Sleep -Seconds 2
        notepad $latestLog.FullName
    } else {
        Write-Host "⚠ No OBS log files found" -ForegroundColor Yellow
        Write-Host "  Launch OBS at least once to generate logs" -ForegroundColor Yellow
    }
} else {
    Write-Host "⚠ OBS log directory not found: $logPath" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
