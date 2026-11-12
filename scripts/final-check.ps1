$ErrorActionPreference = 'SilentlyContinue'
$T = Join-Path $env:LOCALAPPDATA 'Programs\Flux'
$c = Join-Path $T 'settings\Flux.conf'

Push-Location $T
$null = & '.\flux.exe' --version 2>&1
$gui = $LASTEXITCODE
$null = & '.\flux-core.exe' --version 2>&1
$core = $LASTEXITCODE
Pop-Location
Start-Sleep -Seconds 2
Write-Output ('GUI_EXIT=' + $gui)
Write-Output ('CORE_EXIT=' + $core)
Write-Output ('CONF_BEFORE=' + ((Get-Content $c -Raw) -replace "`r`n", '|'))
Set-Content -Path $c -Encoding ASCII -Value '[core]'
Start-Sleep -Seconds 2
Write-Output ('CONF_FINAL=' + ((Get-Content $c -Raw) -replace "`r`n", '|'))

$ws = New-Object -ComObject WScript.Shell
foreach ($p in @(
    (Join-Path $env:USERPROFILE 'Desktop\Flux.lnk'),
    (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Flux.lnk')
)) {
    $s = $ws.CreateShortcut($p)
    Write-Output ('LNK_TARGET=' + $s.TargetPath)
    Write-Output ('LNK_WORKDIR=' + $s.WorkingDirectory)
}

Write-Output ('FILE_COUNT=' + (Get-ChildItem $T -Recurse -File).Count)
Write-Output ('TOTAL_MB=' + [math]::Round((Get-ChildItem $T -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 2))
Write-Output ('CONF_BYTES=' + (Get-Item $c).Length)
Write-Output 'FINAL_OK=1'
