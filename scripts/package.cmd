@echo off
setlocal
rem flux 1.27.0-flux packaging entry (idempotent).
rem Package only; never runs build.cmd / configure.cmd / cmake --build.
rem TMP/TEMP point at <root>\tmp (same fix as build.cmd for windeployqt).
rem Deliberately does NOT add Qt/OpenSSL bin to PATH, so the --version
rem check inside package.ps1 proves the dist folder is self-contained.
set "ROOT=%~dp0.."
if not exist "%ROOT%\tmp" mkdir "%ROOT%\tmp"
set "TMP=%ROOT%\tmp"
set "TEMP=%ROOT%\tmp"
powershell.exe -NoProfile -NoLogo -ExecutionPolicy Bypass -File "%~dp0package.ps1"
set "RC=%ERRORLEVEL%"
echo PACKAGE_EXIT=%RC%
endlocal & exit /b %RC%
