@echo off
REM Configure + build Flux with external Qt and conda OpenSSL.
setlocal
set ROOT=C:\Users\Administrator\Desktop\Quick\deskflow-lingxi
set FLUXSRC=%ROOT%\deskflow
set QT=%ROOT%\tools\Qt\6.8.3\msvc2022_64
set OSSL=%ROOT%\tools\openssl\Library

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo VCVARS FAILED & exit /b 1)

set PATH=%QT%\bin;%OSSL%\bin;%PATH%

if not exist "%ROOT%\build" mkdir "%ROOT%\build"
cd /d "%ROOT%\build"

"%ROOT%\tools\cmake\bin\cmake.exe" --version >nul 2>&1
set CMAKE_EXE=cmake

%CMAKE_EXE% -S "%FLUXSRC%" -B "%ROOT%\build" -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT%;%OSSL%" ^
  -DOpenSSL_ROOT_DIR="%OSSL%" ^
  -DOpenSSL_INCLUDE_DIR="%OSSL%\include" ^
  -DOpenSSL_SSL_LIBRARY="%OSSL%\lib\libssl.lib" ^
  -DOpenSSL_CRYPTO_LIBRARY="%OSSL%\lib\libcrypto.lib" ^
  -DSKIP_BUILD_TESTS=ON ^
  -DBUILD_INSTALLER=OFF ^
  -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF ^
  -DPACKAGE_VERSION_LABEL=flux

echo CONFIGURE_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
