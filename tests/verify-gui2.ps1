param(
  [string]$Root = '',
  [string]$Serial = ''
)

# Supplementary acceptance: the checks verify.ps1 cannot cover.
#   G1 main window exists + drag-drop wired (static assert on MainForm.cs + live window)
#   G2 visible change during transfer (multi-frame screenshot hash compare)
#   G3 explicit error dialog when hdc server is unavailable
#
# IMPORTANT: this script deliberately contains NO non-ASCII literals.
# PowerShell 5.1 decodes a BOM-less .ps1 as GB2312, silently mangling CJK literals,
# which made FindWindow by Chinese title fail on this machine. Avoid CJK entirely.

$ErrorActionPreference = 'Continue'

# ---- Default Root: parent of the directory holding this script ----
if ([string]::IsNullOrEmpty($Root)) {
  $Root = Split-Path -Parent $PSScriptRoot
}

# ---- Locate hdc.exe: program dir > LINGXI_HDC > PATH > DevEco Studio default ----
$hdc = Join-Path $Root 'hdc.exe'
if (-not (Test-Path -LiteralPath $hdc)) {
  if ($env:LINGXI_HDC -and (Test-Path -LiteralPath $env:LINGXI_HDC)) {
    $hdc = $env:LINGXI_HDC
  } else {
    $cmd = Get-Command 'hdc.exe' -ErrorAction SilentlyContinue
    if ($cmd) {
      $hdc = $cmd.Source
    } else {
      $hdc = Join-Path $env:ProgramFiles 'Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
    }
  }
}

$exe = Join-Path $Root 'Lingxi.exe'
$src = Join-Path $Root 'src\MainForm.cs'
$Remote = '/storage/media/100/local/files/Docs/Desktop'
$pass = 0
$fail = 0

function Report($ok, $name, $detail) {
  if ($ok) { $script:pass++; Write-Host ("[PASS] " + $name + " | " + $detail) }
  else { $script:fail++; Write-Host ("[FAIL] " + $name + " | " + $detail) }
}

Add-Type -AssemblyName System.Windows.Forms, System.Drawing

Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumChildWindows(IntPtr h, EnumWindowsProc cb, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int m);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
'@

Add-Type -Namespace W3 -Name U3 -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
public struct RECT { public int Left, Top, Right, Bottom; }
'@

function Get-WindowMapOfPid($targetPid) {
  $script:targetPid = $targetPid
  $script:result = @{}
  $cb = [W.U+EnumWindowsProc]{
    param($ch, $l)
    $p2 = 0
    [void][W.U]::GetWindowThreadProcessId($ch, [ref]$p2)
    if ($p2 -eq $script:targetPid) {
      if ([W.U]::IsWindowVisible($ch)) {
        $sb = New-Object System.Text.StringBuilder 512
        [void][W.U]::GetWindowTextW($ch, $sb, 512)
        $script:result[$ch] = $sb.ToString()
      }
    }
    return $true
  }
  [void][W.U]::EnumWindows($cb, [IntPtr]::Zero)
  return $script:result
}

function Get-ChildTexts($hwnd) {
  $script:sbAll = New-Object System.Text.StringBuilder
  $cb2 = [W.U+EnumWindowsProc]{
    param($ch, $l)
    $sb = New-Object System.Text.StringBuilder 512
    [void][W.U]::GetWindowTextW($ch, $sb, 512)
    $t = $sb.ToString()
    if ($t.Length -gt 0) { [void]$script:sbAll.Append($t + ' ') }
    return $true
  }
  [void][W.U]::EnumChildWindows($hwnd, $cb2, [IntPtr]::Zero)
  return $script:sbAll.ToString().Trim()
}

function Get-MainWindowOfPid($targetPid) {
  $wins = Get-WindowMapOfPid $targetPid
  foreach ($k in $wins.Keys) {
    if ($wins[$k].Length -gt 0) { return @{ hwnd = $k; title = $wins[$k] } }
  }
  if ($wins.Keys.Count -gt 0) {
    $first = @($wins.Keys)[0]
    return @{ hwnd = $first; title = '' }
  }
  return $null
}

# ---------------- G1 ----------------
try {
  $txt = Get-Content -Path $src -Raw -Encoding UTF8
  $hasAllow = $txt -match 'AllowDrop\s*=\s*true'
  $hasEnter = $txt -match 'DragEnter\s*\+='
  $hasDrop  = $txt -match 'DragDrop\s*\+='

  $p = Start-Process -FilePath $exe -PassThru
  Start-Sleep -Seconds 3
  $alive = -not $p.HasExited
  $mw = $null
  if ($alive) { $mw = Get-MainWindowOfPid $p.Id }
  if (-not $p.HasExited) { $p | Stop-Process -Force -ErrorAction SilentlyContinue }

  $winOk = ($mw -ne $null) -and ($mw.title.Length -gt 0)
  $tp = ''
  if ($mw -ne $null) { $tp = $mw.title }
  Report ($hasAllow -and $hasEnter -and $hasDrop -and $alive -and $winOk) 'G1 window-and-dragdrop' `
    ("AllowDrop=" + $hasAllow + " DragEnter=" + $hasEnter + " DragDrop=" + $hasDrop + " alive=" + $alive + " titled-window=" + $winOk + " title=[" + $tp + "]")
} catch { Report $false 'G1 window-and-dragdrop' ("exception: " + $_.Exception.Message) }

# ---------------- G2 ----------------
try {
  $tmp = Join-Path $env:TEMP ('ht_g2_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
  New-Item -ItemType Directory -Path $tmp -Force | Out-Null
  $big = Join-Path $tmp 'g2big.bin'
  $fs = [System.IO.File]::Create($big)
  $buf = New-Object byte[] (1024*1024)
  $rnd = New-Object System.Random
  for ($i = 0; $i -lt 200; $i++) { $rnd.NextBytes($buf); $fs.Write($buf, 0, $buf.Length) }
  $fs.Close()
  $size = (Get-Item $big).Length

  $p = Start-Process -FilePath $exe -ArgumentList ('"' + $big + '"') -PassThru
  Start-Sleep -Milliseconds 1200

  $mw = Get-MainWindowOfPid $p.Id
  $shots = @()
  if ($mw -ne $null) {
    [void][W3.U3]::SetForegroundWindow($mw.hwnd)
    Start-Sleep -Milliseconds 300
    for ($k = 0; $k -lt 4; $k++) {
      $r = New-Object W3.U3+RECT
      [void][W3.U3]::GetWindowRect($mw.hwnd, [ref]$r)
      $w = $r.Right - $r.Left
      $hgt = $r.Bottom - $r.Top
      if ($w -gt 0 -and $hgt -gt 0) {
        $bmp = New-Object System.Drawing.Bitmap $w, $hgt
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen((New-Object System.Drawing.Point $r.Left, $r.Top), [System.Drawing.Point]::Empty, (New-Object System.Drawing.Size $w, $hgt))
        $g.Dispose()
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $shots += [System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash($ms.ToArray()))
        $bmp.Dispose(); $ms.Dispose()
      }
      Start-Sleep -Milliseconds 900
    }
  }
  $frames = $shots.Count
  $distinct = 0
  if ($frames -gt 0) { $distinct = ($shots | Select-Object -Unique).Count }

  $dialogSeen = ''
  for ($w = 0; $w -lt 120; $w++) {
    if ($p.HasExited) { $dialogSeen = 'process-exited'; break }
    $wins = Get-WindowMapOfPid $p.Id
    foreach ($k in $wins.Keys) {
      $t = $wins[$k]
      if ($t.Length -gt 0 -and ($mw -eq $null -or $k -ne $mw.hwnd)) { $dialogSeen = $t; break }
    }
    if ($dialogSeen.Length -gt 0) { break }
    Start-Sleep -Milliseconds 500
  }
  if (-not $p.HasExited) { $p | Stop-Process -Force -ErrorAction SilentlyContinue }

  & $hdc -t $Serial shell ("rm -f " + $Remote + "/g2big.bin") 2>$null | Out-Null
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

  Report ($frames -ge 3 -and $distinct -ge 2) 'G2 progress-visible' `
    ("frames=" + $frames + " distinct=" + $distinct + " size_mb=" + [math]::Round($size/1MB,1) + " end=[" + $dialogSeen + "]")
} catch { Report $false 'G2 progress-visible' ("exception: " + $_.Exception.Message) }

# ---------------- G3 ----------------
try {
  $tmp = Join-Path $env:TEMP ('ht_g3_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
  New-Item -ItemType Directory -Path $tmp -Force | Out-Null
  $f = Join-Path $tmp 'g3.txt'
  Set-Content -Path $f -Value 'x' -Encoding ASCII

  & $hdc kill 2>$null | Out-Null
  Start-Sleep -Milliseconds 600

  $p = Start-Process -FilePath $exe -ArgumentList ('"' + $f + '"') -PassThru
  $gotDialog = $false
  $dlgTitle = ''
  $dlgText = ''
  for ($w = 0; $w -lt 60; $w++) {
    if ($p.HasExited) { break }
    $wins = Get-WindowMapOfPid $p.Id
    foreach ($k in $wins.Keys) {
      $t = $wins[$k]
      if ($t.Length -gt 0) {
        $ct = Get-ChildTexts $k
        if ($ct.Length -gt 0) {
          $gotDialog = $true; $dlgTitle = $t; $dlgText = $ct
          [void][W.U]::PostMessageW($k, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
          break
        }
      }
    }
    if ($gotDialog) { break }
    Start-Sleep -Milliseconds 500
  }
  $exitCode = -1
  if ($p.HasExited) { $exitCode = $p.ExitCode }
  else { $p | Stop-Process -Force -ErrorAction SilentlyContinue }

  Report ($gotDialog -and $dlgText.Length -gt 0) 'G3 error-message-dialog' `
    ("dialog=" + $gotDialog + " title=[" + $dlgTitle + "] text=[" + $dlgText + "] exit=" + $exitCode)

  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
} catch { Report $false 'G3 error-message-dialog' ("exception: " + $_.Exception.Message) }

& $hdc list targets 2>$null | Out-Null

Write-Host ''
Write-Host ("supplementary summary: PASS=" + $pass + " FAIL=" + $fail)
if ($fail -gt 0) { exit 1 }
exit 0
