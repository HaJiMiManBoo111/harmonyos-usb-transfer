@echo off
setlocal
set "ROOT=%~dp0.."
if not exist "%ROOT%\tmp" mkdir "%ROOT%\tmp"
set "TMP=%ROOT%\tmp"
set "TEMP=%ROOT%\tmp"
set "PATH=%ROOT%\tools\Qt\6.8.3\msvc2022_64\bin;%ROOT%\tools\openssl\Library\bin;%PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-final.ps1"
echo PACKAGE_EXIT=%ERRORLEVEL%
endlocal & exit /b %ERRORLEVEL%
