@echo off
REM Double-click me on Windows to flash your device.
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
  py flash.py %*
  goto done
)
where python >nul 2>nul
if %errorlevel%==0 (
  python flash.py %*
  goto done
)

echo.
echo   Oops -- I need Python 3, and it doesn't look installed.
echo.
echo   The easy fix (takes 2 minutes):
echo     1. Go to https://www.python.org/downloads/
echo     2. Download and run the installer.
echo     3. IMPORTANT: tick "Add Python to PATH" on the first screen.
echo     4. Double-click this file again.
echo.

:done
echo.
pause
