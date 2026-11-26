@echo off
REM Build OpenSSL 3.5.4 with MSVC x64
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-openssl.ps1"
exit /b %errorlevel%
