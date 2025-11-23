<#
.SYNOPSIS
    Builds SVT-JPEG-XS and the OBS Plugin on Windows.
    
.DESCRIPTION
    This script automates the build process for Windows.
    Prerequisites:
    - Visual Studio 2019 or 2022 (C++ Desktop Development)
    - CMake 3.16+
    - NASM (Netwide Assembler) in PATH (for SVT-JPEG-XS optimizations)
    - OBS Studio installed (libs will be generated from DLLs)
    - SRT and OpenSSL installed (default: C:\Program Files\libsrt, C:\Program Files\OpenSSL-Win64)
    - Qt6 installed (default: C:\Qt\6.x.x\msvc2019_64)

.EXAMPLE
    .\build_windows.ps1
#>

$ErrorActionPreference = "Stop"

# Configuration
$SvtVersion = "0.10.0"
$BuildType = "Release"
$RootDir = Get-Location
$InstallDir = Join-Path $RootDir "install"
$SvtDir = Join-Path $RootDir "SVT-JPEG-XS"
$PluginDir = Join-Path $RootDir "obs-jpegxs-plugin"

# Check for Dependencies
if (!(Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Please install CMake."
}

# Check for NASM (Required for SVT x86 ASM)
if (!(Get-Command nasm -ErrorAction SilentlyContinue)) {
    Write-Warning "NASM not found. SVT-JPEG-XS performance will be degraded (C-only). Install NASM and add to PATH for AVX2/AVX512 support."
}

# ------------------------------------------------------------------------------
# 1. Build SVT-JPEG-XS
# ------------------------------------------------------------------------------
Write-Host "`n=== Building SVT-JPEG-XS ===`n" -ForegroundColor Cyan

$SvtBuildDir = Join-Path $SvtDir "build"
if (!(Test-Path $SvtBuildDir)) { New-Item -ItemType Directory -Path $SvtBuildDir | Out-Null }

Push-Location $SvtBuildDir

# Configure
# Note: SVT-JPEG-XS usually defaults to static libs unless BUILD_SHARED_LIBS=ON
cmake .. `
    -DCMAKE_INSTALL_PREFIX="$InstallDir/svt-jpegxs" `
    -DBUILD_SHARED_LIBS=ON `
    -DBUILD_APPS=OFF `
    -DENABLE_APPS=OFF `
    -DCMAKE_BUILD_TYPE=$BuildType

# Build & Install
cmake --build . --config $BuildType --target install

if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "Failed to build SVT-JPEG-XS"
}

Pop-Location

# ------------------------------------------------------------------------------
# 2. Build OBS Plugin
# ------------------------------------------------------------------------------
Write-Host "`n=== Building OBS JPEG XS Plugin ===`n" -ForegroundColor Cyan

$PluginBuildDir = Join-Path $PluginDir "build_win"
if (!(Test-Path $PluginBuildDir)) { New-Item -ItemType Directory -Path $PluginBuildDir | Out-Null }

Push-Location $PluginBuildDir

# Environment checks for OBS/SRT/Qt
# We allow user to override paths via Env Vars, otherwise CMake defaults apply

Write-Host "Configuring Plugin..."
cmake .. `
    -DCMAKE_PREFIX_PATH="$InstallDir/svt-jpegxs" `
    -DSVTJPEGXS_INCLUDE_DIRS="$InstallDir/svt-jpegxs/include" `
    -DSVTJPEGXS_LIBRARIES="$InstallDir/svt-jpegxs/lib/SvtJpegxs.lib" `
    -DCMAKE_BUILD_TYPE=$BuildType

# Build
cmake --build . --config $BuildType

if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "Failed to build OBS Plugin"
}

# ------------------------------------------------------------------------------
# 3. Install / Package
# ------------------------------------------------------------------------------
Write-Host "`n=== Installation ===`n" -ForegroundColor Cyan

# Check standard OBS plugin path or user defined
$ObsPluginDir = "C:\Program Files\obs-studio\obs-plugins\64bit"
if (Test-Path $ObsPluginDir) {
    Write-Host "Found OBS Studio at default location. Installing..."
    
    # Copy DLLs
    $BuildOutput = "$PluginBuildDir/$BuildType"
    Copy-Item "$BuildOutput/obs-jpegxs-input.dll" -Destination $ObsPluginDir -Force
    Copy-Item "$BuildOutput/obs-jpegxs-output.dll" -Destination $ObsPluginDir -Force
    
    # Copy SVT DLL if needed (it's shared)
    Copy-Item "$InstallDir/svt-jpegxs/bin/SvtJpegxs.dll" -Destination "C:\Program Files\obs-studio\bin\64bit" -Force
    
    # Copy Resources (locale, etc if any)
    $DataDir = "C:\Program Files\obs-studio\data\obs-plugins\obs-jpegxs-plugin"
    if (!(Test-Path $DataDir)) { New-Item -ItemType Directory -Path $DataDir -Force | Out-Null }
    # Copy-Item "$PluginDir/data/*" -Destination $DataDir -Recurse -Force
    
    Write-Host "Installed successfully to C:\Program Files\obs-studio" -ForegroundColor Green
} else {
    Write-Warning "OBS Studio not found at C:\Program Files\obs-studio. Plugins built but not installed."
    Write-Host "DLLs are located in: $PluginBuildDir\$BuildType"
}

Pop-Location
