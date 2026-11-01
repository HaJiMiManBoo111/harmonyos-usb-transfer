@echo off
setlocal
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
  echo csc.exe not found. .NET Framework 4 is required.
  pause
  exit /b 1
)
set "SRC=%~dp0src"
"%CSC%" /nologo /target:winexe /out:"%~dp0Lingxi.exe" /win32icon:"%~dp0res\app.ico" /win32manifest:"%~dp0res\app.manifest" /r:System.Windows.Forms.dll /r:System.Drawing.dll "%SRC%\HdcCore.cs" "%SRC%\Program.cs" "%SRC%\MainForm.cs" "%SRC%\Fluent.cs"
if errorlevel 1 (
  echo.
  echo BUILD FAILED
  pause
  exit /b 1
)
echo.
echo BUILD OK - Lingxi.exe
pause
