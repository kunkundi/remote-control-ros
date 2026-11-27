@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0src\remote_drive\dozer_rtc_sdk\export_windows.ps1" %*
if errorlevel 1 (
  echo RTC SDK export failed.
  if "%~1"=="" pause
  exit /b 1
)
