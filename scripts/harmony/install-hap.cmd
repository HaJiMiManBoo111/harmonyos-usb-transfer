@echo off
REM 安装刚构建的 HAP 到真机
setlocal
set "HDC=C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe"
set "HAP=C:\Users\Administrator\Desktop\Quick\deskflow-win-pack\deskflow-harmonyos-main\entry\build\default\outputs\default\entry-default-signed.hap"

echo === hdc version ===
"%HDC%" version
echo === targets ===
"%HDC%" list targets
echo === install ===
"%HDC%" install -r "%HAP%"
echo INSTALL_RC=%ERRORLEVEL%
