@echo off
setlocal EnableDelayedExpansion

echo ========================================
echo   OBS JPEG XS Plugin Installer (Batch)
echo ========================================
echo.

:: Set paths
set "SCRIPT_DIR=%~dp0"
set "OBS_PATH=C:\Program Files\obs-studio"
set "PLUGIN_BUILD_DIR=%SCRIPT_DIR%obs-jpegxs-plugin\build\Release"
set "SVT_BUILD_DIR=%SCRIPT_DIR%SVT-JPEG-XS\Bin\Release\Release"

:: Check OBS Path
if not exist "%OBS_PATH%\bin\64bit\obs64.exe" (
    echo [ERROR] OBS Studio not found at %OBS_PATH%
    echo Please check your installation.
    pause
    exit /b 1
)

:: Check if OBS is running
tasklist /FI "IMAGENAME eq obs64.exe" 2>NUL | find /I /N "obs64.exe">NUL
if "%ERRORLEVEL%"=="0" (
    echo [WARNING] OBS is running. Attempting to close...
    taskkill /F /IM obs64.exe >NUL
    timeout /t 2 /nobreak >NUL
)

echo [1/3] Installing Plugin DLLs...
if not exist "%OBS_PATH%\obs-plugins\64bit" mkdir "%OBS_PATH%\obs-plugins\64bit"

copy /Y "%PLUGIN_BUILD_DIR%\obs-jpegxs-output.dll" "%OBS_PATH%\obs-plugins\64bit\"
if %errorlevel% neq 0 goto :error
echo   - Installed obs-jpegxs-output.dll

copy /Y "%PLUGIN_BUILD_DIR%\obs-jpegxs-input.dll" "%OBS_PATH%\obs-plugins\64bit\"
if %errorlevel% neq 0 goto :error
echo   - Installed obs-jpegxs-input.dll

echo [2/3] Installing SVT-JPEG-XS Library...
copy /Y "%SVT_BUILD_DIR%\SvtJpegxs.dll" "%OBS_PATH%\bin\64bit\"
if %errorlevel% neq 0 goto :error
echo   - Installed SvtJpegxs.dll

echo.
echo [3/3] Installation Complete!
echo.
echo You can now start OBS Studio.
echo.
pause
exit /b 0

:error
echo.
echo [ERROR] Installation failed!
pause
exit /b 1
