@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%run_tuxdmx_windows.ps1"

if not exist "%PS_SCRIPT%" (
  echo [ERROR] Missing launcher script: "%PS_SCRIPT%"
  exit /b 1
)

where pwsh >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
) else (
  powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
)

exit /b %ERRORLEVEL%
