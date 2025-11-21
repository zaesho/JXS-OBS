@echo off
echo Installing OBS JPEG XS Plugin...
echo Source: %~dp0obs-jpegxs-plugin\build\Release\obs-jpegxs-output.dll
echo Destination: C:\Program Files\obs-studio\obs-plugins\64bit\obs-jpegxs-output.dll

copy /Y "%~dp0obs-jpegxs-plugin\build\Release\obs-jpegxs-output.dll" "C:\Program Files\obs-studio\obs-plugins\64bit\obs-jpegxs-output.dll"
if %errorlevel% neq 0 (
    echo FAILED to copy obs-jpegxs-output.dll
    echo Error Level: %errorlevel%
    pause
    exit /b %errorlevel%
)

echo Success!
pause
