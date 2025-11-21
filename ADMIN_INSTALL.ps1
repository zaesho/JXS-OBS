# ADMIN INSTALL SCRIPT - Run as Administrator
# This script MUST be run with admin privileges

$ErrorActionPreference = "Stop"

Write-Host "=== JPEG XS Plugin Installation ===" -ForegroundColor Cyan
Write-Host ""

# Check if running as admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator', then run this script again." -ForegroundColor Yellow
    pause
    exit 1
}

# Determine script location
$ScriptPath = $PSScriptRoot
if (-not $ScriptPath) {
    $ScriptPath = Split-Path -Parent $MyInvocation.MyCommand.Definition
}

Write-Host "Step 1: Locating OBS Studio..." -ForegroundColor Yellow
$obsPath = "C:\Program Files\obs-studio"
# Try to find OBS in registry
try {
    $regPath = Get-ItemProperty -Path "HKLM:\SOFTWARE\OBS Studio" -Name "Path" -ErrorAction SilentlyContinue
    if ($regPath) {
        $obsPath = $regPath.Path
    }
} catch {
    # Ignore registry errors, use default
}

if (-not (Test-Path $obsPath)) {
    Write-Host "ERROR: OBS Studio not found at $obsPath" -ForegroundColor Red
    Write-Host "Please install OBS Studio or edit this script with the correct path." -ForegroundColor Yellow
    pause
    exit 1
}
Write-Host "  Found OBS at: $obsPath" -ForegroundColor Green

Write-Host "Step 2: Stopping OBS if running..." -ForegroundColor Yellow
Get-Process obs64 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

Write-Host "Step 3: Installing encoder plugin..." -ForegroundColor Yellow
$sourceDir = Join-Path $ScriptPath "obs-jpegxs-plugin\build\Release"
$destDir = Join-Path $obsPath "obs-plugins\64bit"

if (-not (Test-Path $destDir)) {
    Write-Host "  Creating destination directory..."
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
}

$encoderDll = Join-Path $sourceDir "obs-jpegxs-output.dll"
$decoderDll = Join-Path $sourceDir "obs-jpegxs-input.dll"

if (-not (Test-Path $encoderDll)) {
    Write-Host "ERROR: Encoder plugin not found at: $encoderDll" -ForegroundColor Red
    Write-Host "Please build the project first!" -ForegroundColor Yellow
    pause
    exit 1
}

Copy-Item $encoderDll $destDir -Force
Write-Host "  Copied: obs-jpegxs-output.dll" -ForegroundColor Green

Write-Host "Step 4: Installing decoder plugin..." -ForegroundColor Yellow
if (Test-Path $decoderDll) {
    Copy-Item $decoderDll $destDir -Force
    Write-Host "  Copied: obs-jpegxs-input.dll" -ForegroundColor Green
} else {
    Write-Host "  WARNING: Decoder plugin not found (skipped)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Step 5: Verifying installation..." -ForegroundColor Yellow
$destEncoder = Join-Path $destDir "obs-jpegxs-output.dll"
if (Test-Path $destEncoder) {
    $hash1 = Get-FileHash $encoderDll | Select-Object -ExpandProperty Hash
    $hash2 = Get-FileHash $destEncoder | Select-Object -ExpandProperty Hash
    
    if ($hash1 -eq $hash2) {
        Write-Host "  SUCCESS: Encoder plugin verified!" -ForegroundColor Green
    } else {
        Write-Host "  WARNING: Encoder hash mismatch!" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== Installation Complete ===" -ForegroundColor Green
Write-Host "You can now start OBS and test the JPEG XS plugin." -ForegroundColor Cyan
Write-Host ""
pause
