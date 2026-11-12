@echo off
rem LoWRecomp - installer / update. Double-click to start.
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\Install-LoWRecomp.ps1" %*
echo.
pause
