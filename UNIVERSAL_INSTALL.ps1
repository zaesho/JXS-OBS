# Universal OBS JPEG XS Plugin Installer
# Works on any Windows machine with OBS Studio installed

param(
    [switch]$Force = $false
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OBS JPEG XS Plugin Installer" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Function to find OBS installation
function Find-OBSInstallation {
    Write-Host "[1/6] Searching for OBS Studio installation..." -ForegroundColor Yellow
    
    $possiblePaths = @(
        "${env:ProgramFiles}\obs-studio",
        "${env:ProgramFiles(x86)}\obs-studio",
        "${env:LOCALAPPDATA}\Programs\obs-studio"
    )
    
    foreach ($path in $possiblePaths) {
        if (Test-Path "$path\bin\64bit\obs64.exe") {
            Write-Host "  ✓ Found OBS Studio at: $path" -ForegroundColor Green
            return $path
        }
    }
    
    throw "OBS Studio installation not found. Please install OBS Studio first."
}

# Function to stop OBS if running
function Stop-OBSIfRunning {
    Write-Host "[2/6] Checking if OBS is running..." -ForegroundColor Yellow
    
    $obsProcess = Get-Process -Name "obs64" -ErrorAction SilentlyContinue
    if ($obsProcess) {
        Write-Host "  ⚠ OBS is currently running. Attempting to close..." -ForegroundColor Yellow
        
        try {
            $obsProcess | Stop-Process -Force
            Start-Sleep -Seconds 2
            Write-Host "  ✓ OBS closed successfully" -ForegroundColor Green
        } catch {
            throw "Failed to close OBS. Please close it manually and try again."
        }
    } else {
        Write-Host "  ✓ OBS is not running" -ForegroundColor Green
    }
}

# Function to copy plugin files
function Copy-PluginFiles {
    param(
        [string]$obsPath,
        [string]$pluginSourcePath
    )
    
    Write-Host "[3/6] Installing plugin files..." -ForegroundColor Yellow
    
    $pluginDestPath = "$obsPath\obs-plugins\64bit"
    
    # Ensure directories exist
    if (-not (Test-Path $pluginDestPath)) {
        New-Item -ItemType Directory -Path $pluginDestPath -Force | Out-Null
    }
    
    # Copy plugin DLLs
    $dllFiles = @(
        "obs-jpegxs-input.dll",
        "obs-jpegxs-output.dll"
    )
    
    foreach ($dll in $dllFiles) {
        $sourcePath = "$pluginSourcePath\$dll"
        if (Test-Path $sourcePath) {
            Copy-Item -Path $sourcePath -Destination $pluginDestPath -Force
            Write-Host "  ✓ Installed $dll" -ForegroundColor Green
        } else {
            Write-Host "  ✗ Warning: $dll not found at $sourcePath" -ForegroundColor Red
        }
    }
}

# Function to copy SVT-JPEG-XS library
function Copy-SVTLibrary {
    param(
        [string]$obsPath,
        [string]$svtSourcePath
    )
    
    Write-Host "[4/6] Installing SVT-JPEG-XS library..." -ForegroundColor Yellow
    
    $binPath = "$obsPath\bin\64bit"
    $svtDll = "SvtJpegxsEnc.dll"
    
    if (Test-Path "$svtSourcePath\$svtDll") {
        Copy-Item -Path "$svtSourcePath\$svtDll" -Destination $binPath -Force
        Write-Host "  ✓ Installed $svtDll" -ForegroundColor Green
    } else {
        Write-Host "  ⚠ Warning: $svtDll not found. Plugin may not work correctly." -ForegroundColor Yellow
    }
}

# Main installation
try {
    # Get script directory
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    
    # Find OBS installation
    $obsPath = Find-OBSInstallation
    
    # Stop OBS if running
    Stop-OBSIfRunning
    
    # Define source paths
    $pluginBuildPath = "$scriptDir\obs-jpegxs-plugin\build\Release"
    $svtBuildPath = "$scriptDir\SVT-JPEG-XS\Build\windows\Release"
    
    # Check if build files exist
    Write-Host "[5/6] Verifying build files..." -ForegroundColor Yellow
    if (-not (Test-Path "$pluginBuildPath\obs-jpegxs-input.dll")) {
        throw "Plugin build files not found. Please build the plugin first using: cmake --build obs-jpegxs-plugin\build --config Release"
    }
    Write-Host "  ✓ Build files found" -ForegroundColor Green
    
    # Copy files
    Copy-PluginFiles -obsPath $obsPath -pluginSourcePath $pluginBuildPath
    Copy-SVTLibrary -obsPath $obsPath -svtSourcePath $svtBuildPath
    
    Write-Host "[6/6] Installation complete!" -ForegroundColor Green
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  Installation Summary" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  Plugin installed to: $obsPath\obs-plugins\64bit" -ForegroundColor White
    Write-Host "  Library installed to: $obsPath\bin\64bit" -ForegroundColor White
    Write-Host ""
    Write-Host "You can now start OBS Studio and use the JPEG XS plugins:" -ForegroundColor Green
    Write-Host "  • JPEG XS Source (RTP/SRT) - Input/Decoder" -ForegroundColor White
    Write-Host "  • JPEG XS Output (RTP/SRT) - Output/Encoder" -ForegroundColor White
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  Installation Failed" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host ""
    exit 1
}
