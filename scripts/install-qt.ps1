$ErrorActionPreference = 'Continue'
$root = 'C:\Users\Administrator\Desktop\Quick\deskflow-lingxi'
$log  = Join-Path $root 'logs\install-qt.log'
New-Item -ItemType Directory -Force -Path (Join-Path $root 'logs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $root 'tools\Qt') | Out-Null

"[$(Get-Date -Format o)] start" | Tee-Object -FilePath $log -Append

# qtbase + qtsvg + icu only (deskflow needs Core/Widgets/Network/Gui/Svg)
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 `
  -O (Join-Path $root 'tools\Qt') `
  --archives qtbase qtsvg icu 2>&1 | Tee-Object -FilePath $log -Append

"[$(Get-Date -Format o)] aqt exit=$LASTEXITCODE" | Tee-Object -FilePath $log -Append

$qtRoot = Join-Path $root 'tools\Qt\6.8.3\msvc2022_64'
"exists=$(Test-Path $qtRoot)" | Tee-Object -FilePath $log -Append
if (Test-Path $qtRoot) {
    Get-ChildItem $qtRoot | Select-Object -ExpandProperty Name | Tee-Object -FilePath $log -Append
    "windeployqt=$(Test-Path (Join-Path $qtRoot 'bin\windeployqt.exe'))" | Tee-Object -FilePath $log -Append
}
"[$(Get-Date -Format o)] done" | Tee-Object -FilePath $log -Append
