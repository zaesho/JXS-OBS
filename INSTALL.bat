@echo off
echo ============================================
echo JPEG XS Plugin Installer
echo ============================================
echo.
echo This script will launch the PowerShell installer with Admin privileges.
echo.

powershell -Command "Start-Process powershell -Verb RunAs -ArgumentList '-ExecutionPolicy Bypass -File \"%~dp0ADMIN_INSTALL.ps1\"'"

if %ERRORLEVEL% NEQ 0 (
    echo Failed to launch installer.
    pause
)
