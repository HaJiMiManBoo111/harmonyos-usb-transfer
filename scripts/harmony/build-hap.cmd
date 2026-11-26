@echo off
REM 构建鸿蒙端 Flux 客户端 HAP（release）
REM 用法：build-hap.cmd
REM 产出：%PROJ%\entry\build\default\outputs\default\entry-default-signed.hap
setlocal enabledelayedexpansion

set "DEVECO=C:\Program Files\Huawei\DevEco Studio"
set "PROJ=C:\Users\Administrator\Desktop\Quick\deskflow-win-pack\deskflow-harmonyos-main"

set "DEVECO_SDK_HOME=%DEVECO%\sdk"
set "NODE_HOME=%DEVECO%\tools\node"
set "JAVA_HOME=%DEVECO%\jbr"
set "PATH=%DEVECO%\tools\node;%DEVECO%\tools\ohpm\bin;%DEVECO%\sdk\default\openharmony\toolchains;%DEVECO%\sdk\default\hms\toolchains;%DEVECO%\jbr\bin;%PATH%"

cd /d "%PROJ%" || exit /b 1
"%DEVECO%\tools\node\node.exe" "%DEVECO%\tools\hvigor\bin\hvigorw.js" --mode module -p product=default -p buildMode=release assembleHap --no-daemon
echo HVIGOR_RC=%ERRORLEVEL%
