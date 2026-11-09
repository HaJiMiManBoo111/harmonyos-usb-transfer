@echo off
REM probe device state: targets, usb, abilities, files dir
setlocal
set "HDC=C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe"
set "T=59C0226421001606"

echo === targets -v ===
"%HDC%" list targets -v
echo === module info ===
"%HDC%" -t %T% shell bm dump -n com.deskflow.ohospoc 2>&1 | findstr /C:"abilityName" /C:"bundleName" /C:"srcEntrance" /C:"processMode"
echo === files dir ===
"%HDC%" -t %T% shell "ls -l /data/app/el2/100/base/com.deskflow.ohospoc/haps/entry/files/" 2>&1
echo === running processes ===
"%HDC%" -t %T% shell "ps -A | grep -i deskflow" 2>&1
