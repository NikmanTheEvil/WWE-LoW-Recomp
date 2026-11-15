@echo off
rem RecompLauncher setup step (see recomplauncher.json): builds LoWRecomp in this
rem folder from the player's own copy of the game, without questions, and prints
rem "progress <0-1> <text>" lines. Players without RecompLauncher use install.bat.
setlocal
cd /d "%~dp0.."
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Install-LoWRecomp.ps1" -Prepare %*
exit /b %ERRORLEVEL%
