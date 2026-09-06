@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-gateway.ps1" %*
if errorlevel 1 (
    echo.
    echo Flashing did not complete. See the error above.
    pause
    exit /b 1
)
echo.
echo Flashing completed. This window can now be closed.
pause
endlocal
