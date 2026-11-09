@echo off
REM Build Flux (Release, x64). Sets TMP/TEMP because windeployqt needs a writable temp dir.
setlocal
set ROOT=C:\Users\Administrator\Desktop\Quick\deskflow-lingxi
set QT=%ROOT%\tools\Qt\6.8.3\msvc2022_64
set OSSL=%ROOT%\tools\openssl\Library
if not exist "%ROOT%\tmp" mkdir "%ROOT%\tmp"
set TMP=%ROOT%\tmp
set TEMP=%ROOT%\tmp
set PATH=%QT%\bin;%OSSL%\bin;%PATH%

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo VCVARS FAILED & exit /b 1)

cmake --build "%ROOT%\build" --config Release -j 8
echo BUILD_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
