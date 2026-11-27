@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_rtc_console.ps1" %*
exit /b %ERRORLEVEL%
