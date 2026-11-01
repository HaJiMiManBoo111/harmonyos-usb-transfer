<#
  灵犀 (Lingxi) 验收脚本 —— 发送方向
  逐条验证 docs/ARCHITECTURE.md「验收标准」，输出 [PASS]/[FAIL]，最后汇总 PASS=n FAIL=m，有 FAIL 则 exit 1。

  用法:
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify.ps1
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify.ps1 -Root <dir> -Serial <sn> -Hdc <path>

  T1 exe 存在且能启动 / T2 hdc 可用 / T3 设备在线 / T4 静默模式传输 /
  T5 设备侧 ls -l 核对 / T7 MessageBox 结果文案 / T6 清理（cleanup 总是最后跑）

  幂等：每次运行前先清掉上次残留在设备侧/本地的测试文件。
#>
param(
    [string]$Root = '',
    [string]$Serial = '',
    [string]$Hdc = ''
)

$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

# ---- 默认 Root：脚本所在目录 (tests) 的上一级 ----
if ([string]::IsNullOrEmpty($Root)) {
    $Root = Split-Path -Parent $PSScriptRoot
}

# ---- 定位 hdc.exe：参数 > 程序同目录 > LINGXI_HDC > PATH > DevEco Studio 默认路径 ----
if ([string]::IsNullOrEmpty($Hdc)) {
    $cand = Join-Path $Root 'hdc.exe'
    if (Test-Path $cand) {
        $Hdc = $cand
    } elseif ($env:LINGXI_HDC -and (Test-Path $env:LINGXI_HDC)) {
        $Hdc = $env:LINGXI_HDC
    } else {
        $cmd = Get-Command 'hdc.exe' -ErrorAction SilentlyContinue
        if ($cmd) {
            $Hdc = $cmd.Source
        } else {
            $devEco = Join-Path $env:ProgramFiles 'Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
            if (Test-Path $devEco) { $Hdc = $devEco }
        }
    }
}

$hdc    = $Hdc
$exe    = Join-Path $Root 'Lingxi.exe'
$Remote = '/storage/media/100/local/files/Docs/Desktop'

# 测试用的固定文件名（幂等：每次都是同一批，跑前先删）
$CnName   = '中文 文件名.txt'
$AsciiName = 'ascii.txt'
$DirName   = 'dirtest'

$script:Pass = 0
$script:Fail = 0

function Write-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) {
        $script:Pass++
        Write-Host ("[PASS] {0} | {1}" -f $Name, $Detail) -ForegroundColor Green
    } else {
        $script:Fail++
        Write-Host ("[FAIL] {0} | {1}" -f $Name, $Detail) -ForegroundColor Red
    }
}

function Write-Info {
    param([string]$Text)
    Write-Host ("[INFO] " + $Text) -ForegroundColor DarkGray
}

# 参数里含空格或引号时补上双引号，供 Start-Process 拼接命令行
function ConvertTo-QuotedArg {
    param([string]$Value)
    if ($null -eq $Value) { return '""' }
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

<#
  统一的外部进程调用：重定向到临时文件再用 UTF-8 读回。
  不用 & 运算符，原因有三个：hdc 的 progress 输出会把 stderr 变成异常、
  需要超时控制、需要拿回原始字节按 UTF-8 解码（中文文件名）。
#>
function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [int]$TimeoutSec = 60
    )
    $outFile = [System.IO.Path]::GetTempFileName()
    $errFile = [System.IO.Path]::GetTempFileName()
    $result = [pscustomobject]@{
        ExitCode  = $null
        Output    = ''
        TimedOut  = $false
        Arguments = ''
    }
    try {
        $result.Arguments = (($Arguments | ForEach-Object { ConvertTo-QuotedArg $_ }) -join ' ')
        $proc = Start-Process -FilePath $FilePath -ArgumentList $result.Arguments -NoNewWindow -PassThru `
                              -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
            $result.TimedOut = $true
            try { $proc.Kill() } catch { }
        }
        $text = ''
        try { $text += [System.IO.File]::ReadAllText($outFile, [System.Text.Encoding]::UTF8) } catch { }
        try { $text += [System.IO.File]::ReadAllText($errFile, [System.Text.Encoding]::UTF8) } catch { }
        $result.Output = $text
        if (-not $result.TimedOut) {
            try { $result.ExitCode = $proc.ExitCode } catch { }
        }
    } catch {
        $result.Output = 'START-FAILED: ' + $_.Exception.Message
    } finally {
        Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
    return $result
}

function Invoke-Hdc {
    param([string[]]$Arguments, [int]$TimeoutSec = 60)
    return Invoke-Native -FilePath $hdc -Arguments $Arguments -TimeoutSec $TimeoutSec
}

# ---- Win32 助手（T7 用）：按 pid 找顶层窗口、读标题与子控件文本、发 WM_CLOSE ----
$winSrc = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class HTVerifyWin
{
    private delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")]
    private static extern bool EnumChildWindows(IntPtr hWnd, EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll")]
    private static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hWnd);

    private static List<IntPtr> _buf = new List<IntPtr>();

    private static bool CollectTop(IntPtr hWnd, IntPtr lParam) { _buf.Add(hWnd); return true; }
    private static bool CollectChild(IntPtr hWnd, IntPtr lParam) { _buf.Add(hWnd); return true; }

    public static IntPtr[] TopWindowsOfPid(int pid)
    {
        _buf = new List<IntPtr>();
        EnumWindows(new EnumProc(CollectTop), IntPtr.Zero);
        List<IntPtr> res = new List<IntPtr>();
        for (int i = 0; i < _buf.Count; i++)
        {
            uint wpid;
            GetWindowThreadProcessId(_buf[i], out wpid);
            if ((int)wpid == pid) { res.Add(_buf[i]); }
        }
        return res.ToArray();
    }

    public static string TextOf(IntPtr hWnd)
    {
        StringBuilder sb = new StringBuilder(4096);
        GetWindowTextW(hWnd, sb, sb.Capacity);
        return sb.ToString();
    }

    public static string ClassOf(IntPtr hWnd)
    {
        StringBuilder sb = new StringBuilder(512);
        GetClassNameW(hWnd, sb, sb.Capacity);
        return sb.ToString();
    }

    public static bool Visible(IntPtr hWnd) { return IsWindowVisible(hWnd); }

    // 拼接窗口下所有子控件文本：MessageBox 正文在 Static 里
    public static string ChildText(IntPtr hWnd)
    {
        _buf = new List<IntPtr>();
        EnumChildWindows(hWnd, new EnumProc(CollectChild), IntPtr.Zero);
        StringBuilder all = new StringBuilder();
        for (int i = 0; i < _buf.Count; i++)
        {
            string t = TextOf(_buf[i]);
            if (t != null && t.Length > 0)
            {
                all.Append(ClassOf(_buf[i]));
                all.Append("=");
                all.Append(t);
                all.Append(" | ");
            }
        }
        return all.ToString();
    }

    public static bool Close(IntPtr hWnd)
    {
        return PostMessageW(hWnd, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }
}
'@

$winHelperOk = $false
if ($null -eq ('HTVerifyWin' -as [type])) {
    try {
        Add-Type -TypeDefinition $winSrc -Language CSharp -ErrorAction Stop
        $winHelperOk = $true
    } catch {
        Write-Info ('Win32 助手编译失败（T7 将 FAIL 并说明）: ' + $_.Exception.Message)
    }
} else {
    $winHelperOk = $true
}

# 设备侧目录里的文件名列表（按行）
function Get-RemoteNames {
    param([string]$TimeoutSec = 30)
    $r = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "ls -1 `"$Remote`"") -TimeoutSec $TimeoutSec
    if ($null -eq $r.Output) { return @() }
    return @($r.Output -split "\r?\n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
}

# 设备侧删除若干条目（文件或目录），静默
function Remove-RemoteItems {
    param([string[]]$Names)
    if ($null -eq $Names -or $Names.Count -eq 0) { return }
    $parts = @($Names | ForEach-Object { "rm -rf `"$Remote/$_`"" })
    $null = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', ($parts -join '; ')) -TimeoutSec 60
}

Write-Host ''
Write-Host ('=' * 78)
Write-Host '灵犀 (Lingxi) 验收 · 发送方向 (tests/verify.ps1)'
Write-Host ('  Root   : ' + $Root)
Write-Host ('  Serial : ' + $Serial)
Write-Host ('  Remote : ' + $Remote)
Write-Host ('  Time   : ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Host ('=' * 78)

# ---------------------------------------------------------------- T1: exe 存在且能启动
Write-Host ''
Write-Host '--- [T1] Lingxi.exe 存在且能启动 ---'
$exeExists = Test-Path -LiteralPath $exe
if (-not $exeExists) {
    Write-Result 'T1 exe-exists-and-starts' $false ("未找到 {0}（src 可能还没编译）" -f $exe)
} else {
    $size = (Get-Item -LiteralPath $exe).Length
    $proc = $null
    $alive = $false
    $note = ''
    try {
        $proc = Start-Process -FilePath $exe -PassThru
        Start-Sleep -Seconds 2
        $alive = -not $proc.HasExited
        if (-not $alive) {
            try { $note = ("进程 2 秒内退出，ExitCode={0}" -f $proc.ExitCode) } catch { $note = '进程 2 秒内退出' }
        }
    } catch {
        $note = 'Start-Process 失败: ' + $_.Exception.Message
        $alive = $false
    } finally {
        if ($null -ne $proc -and -not $proc.HasExited) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch { }
            Start-Sleep -Milliseconds 300
        }
    }
    if ($alive) {
        Write-Result 'T1 exe-exists-and-starts' $true ("{0} ({1} bytes)，启动后 2s 仍存活，已 Stop-Process" -f (Split-Path $exe -Leaf), $size)
    } else {
        Write-Result 'T1 exe-exists-and-starts' $false ("{0} 存在但启动失败: {1}" -f (Split-Path $exe -Leaf), $note)
    }
}

# ---------------------------------------------------------------- T2: hdc 可用
Write-Host ''
Write-Host '--- [T2] hdc 可用 ---'
if (-not (Test-Path -LiteralPath $hdc)) {
    Write-Result 'T2 hdc-version' $false ("未找到 {0}" -f $hdc)
} else {
    $r2 = Invoke-Hdc -Arguments @('-v') -TimeoutSec 20
    $line2 = ($r2.Output -split "\r?\n" | Where-Object { $_ -match 'Ver:' } | Select-Object -First 1)
    if ($null -ne $line2) {
        Write-Result 'T2 hdc-version' $true ($line2.Trim())
    } else {
        Write-Result 'T2 hdc-version' $false ("输出不含 'Ver:'，实际: " + (($r2.Output -replace "[\r\n]+", ' ').Trim()))
    }
}

# ---------------------------------------------------------------- T3: 设备在线
Write-Host ''
Write-Host '--- [T3] 设备在线 ---'
$r3 = Invoke-Hdc -Arguments @('list', 'targets', '-v') -TimeoutSec 25
$usbMatches = @([regex]::Matches($r3.Output, '(?m)^\s*(\S+)\s+USB\s+Connected'))
$usbSerials = @($usbMatches | ForEach-Object { $_.Groups[1].Value })
if ($usbMatches.Count -gt 0) {
    Write-Result 'T3 device-online' $true ("USB Connected: " + ($usbSerials -join ', '))
    if ([string]::IsNullOrEmpty($Serial)) {
        $Serial = $usbSerials[0]
        Write-Info ("未指定 -Serial，自动使用第一台 USB 设备: {0}" -f $Serial)
    } elseif (-not ($usbSerials -contains $Serial)) {
        Write-Info ("注意: 在线序列号里没有 -Serial 指定的 {0}，后续 -t {0} 可能失败" -f $Serial)
    }
} else {
    $raw = ($r3.Output -split "\r?\n" | Where-Object { $_ -match '\S' }) -join ' / '
    Write-Result 'T3 device-online' $false ("未匹配 '^\s*(\S+)\s+USB\s+Connected'，实际: " + $raw)
}

# ---------------------------------------------------------------- 准备测试文件 + 幂等清理
Write-Host ''
Write-Host '--- [准备] 测试夹具 & 幂等清理 ---'

$localDir = Join-Path $env:TEMP ('ht_verify_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
$expectedNames = @($AsciiName, $CnName, $DirName)
$fixtureOk = $false
$fixtureNote = ''

try {
    # 先看一眼设备侧是否已存在同名条目（本次运行会覆盖/删除它们，提前告警）
    $preNames = @(Get-RemoteNames -TimeoutSec 30)
    $collisions = @($expectedNames | Where-Object { $preNames -contains $_ })
    if ($collisions.Count -gt 0) {
        Write-Info ("注意: 设备 Desktop 上已存在同名条目，本次会覆盖/删除: " + ($collisions -join ', '))
    }

    # 清设备侧上次残留（同一批名字），保证幂等
    Remove-RemoteItems -Names $expectedNames
    # 再清本地
    $stale = @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'ht_verify_*' -ErrorAction SilentlyContinue)
    foreach ($s in $stale) { Remove-Item -LiteralPath $s.FullName -Recurse -Force -ErrorAction SilentlyContinue }

    New-Item -ItemType Directory -Path $localDir -Force | Out-Null
    $localDirInner = Join-Path $localDir $DirName
    New-Item -ItemType Directory -Path $localDirInner -Force | Out-Null

    $asciiPath = Join-Path $localDir $AsciiName
    $cnPath    = Join-Path $localDir $CnName

    # ascii.txt 几 KB
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt 200; $i++) { [void]$sb.AppendLine(('line {0} - ascii payload for lingxi verification' -f $i)) }
    [System.IO.File]::WriteAllText($asciiPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))

    # 中文+空格文件名
    [System.IO.File]::WriteAllText($cnPath, "中文内容 chinese content`r`n" + $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))

    # 子目录 2 个文件
    $d1 = Join-Path $localDirInner 'inner1.txt'
    $d2 = Join-Path $localDirInner 'inner2.log'
    [System.IO.File]::WriteAllText($d1, 'inner file 1', (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::WriteAllText($d2, 'inner file 2', (New-Object System.Text.UTF8Encoding($false)))

    $fixtureOk = (Test-Path -LiteralPath $asciiPath) -and (Test-Path -LiteralPath $cnPath) -and (Test-Path -LiteralPath $d1) -and (Test-Path -LiteralPath $d2)
    $fixtureNote = ("{0} (ascii {1}B / 中文 {2}B / {3} {4}个文件)" -f $localDir,
        (Get-Item -LiteralPath $asciiPath).Length,
        (Get-Item -LiteralPath $cnPath).Length,
        $DirName, 2)
} catch {
    $fixtureOk = $false
    $fixtureNote = '构造夹具失败: ' + $_.Exception.Message
}
Write-Info ("夹具: " + $fixtureNote)

# ---------------------------------------------------------------- T4: 静默模式传输
Write-Host ''
Write-Host '--- [T4] 静默模式传输（带参数启动，等文件落盘） ---'
$t4Ok = $false
$t4Detail = ''
$exeProc = $null
$t4TimeoutSec = 60

if (-not $exeExists) {
    $t4Detail = "Lingxi.exe 不存在，无法测静默模式"
} elseif (-not $fixtureOk) {
    $t4Detail = "测试夹具没建好，跳过传输"
} else {
    $asciiPath = Join-Path $localDir $AsciiName
    $cnPath    = Join-Path $localDir $CnName
    $dirPath   = Join-Path $localDir $DirName
    $argStr = (($asciiPath, $cnPath, $dirPath) | ForEach-Object { ConvertTo-QuotedArg $_ }) -join ' '
    Write-Info ("启动: `"$exe`" " + $argStr)

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        # 静默模式跑完会弹 MessageBox 阻塞，所以不能 WaitForExit，只能轮询设备侧
        $exeProc = Start-Process -FilePath $exe -ArgumentList $argStr -PassThru
    } catch {
        $t4Detail = 'Start-Process 失败: ' + $_.Exception.Message
        $exeProc = $null
    }

    if ($null -ne $exeProc) {
        $deadline = (Get-Date).AddSeconds($t4TimeoutSec)
        $found = @()
        $lastNames = @()
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 1000
            $lastNames = Get-RemoteNames -TimeoutSec 20
            $found = @($expectedNames | Where-Object { $lastNames -contains $_ })
            if ($found.Count -eq $expectedNames.Count) { break }
            if ($exeProc.HasExited -and $found.Count -eq 0) {
                # 进程早退了，再给一次机会确认不是刚传完
                Start-Sleep -Milliseconds 500
                $lastNames = Get-RemoteNames -TimeoutSec 20
                $found = @($expectedNames | Where-Object { $lastNames -contains $_ })
                break
            }
        }
        $sw.Stop()

        # 关掉 exe 以及它的 MessageBox
        try {
            if (-not $exeProc.HasExited) { Stop-Process -Id $exeProc.Id -Force -ErrorAction SilentlyContinue }
        } catch { }
        Start-Sleep -Milliseconds 300

        $missing = @($expectedNames | Where-Object { $found -notcontains $_ })
        if ($found.Count -eq $expectedNames.Count) {
            $t4Ok = $true
            $t4Detail = ("3 项全部落盘，用时 {0:N1}s" -f $sw.Elapsed.TotalSeconds)
            # 额外信息：子目录里的文件
            $inner = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "ls -1 `"$Remote/$DirName`"") -TimeoutSec 20
            $innerNames = @($inner.Output -split "\r?\n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
            $innerHit = @(@('inner1.txt', 'inner2.log') | Where-Object { $innerNames -contains $_ })
            if ($innerHit.Count -eq 2) {
                Write-Info ("目录内容也已核对: " + ($innerNames -join ', '))
            } else {
                Write-Info ("目录内容不全，实际: " + (($innerNames -join ', ') -replace '^\s*$', '(空)'))
            }
        } else {
            $t4Detail = ("超时 {0}s，缺: {1}；设备当前条目: {2}" -f $t4TimeoutSec, (($missing -join ', ')), (($lastNames -join ', ') -replace '^\s*$', '(空)'))
        }
    }
}
Write-Result 'T4 silent-mode-transfer' $t4Ok $t4Detail

# ---------------------------------------------------------------- T5: 设备侧核对
Write-Host ''
Write-Host '--- [T5] 设备侧 ls -l 核对三个名字 ---'
$r5 = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "ls -l `"$Remote`"") -TimeoutSec 40
$lsOut = [string]$r5.Output
$hit5 = @($expectedNames | Where-Object { $lsOut.Contains($_) })
$miss5 = @($expectedNames | Where-Object { -not $lsOut.Contains($_) })
if ($miss5.Count -eq 0) {
    $lines = @($lsOut -split "\r?\n" | Where-Object { $n = $_.Trim(); ($n -ne '') -and ($expectedNames | Where-Object { $n.Contains($_) }) })
    Write-Result 'T5 remote-files-listed' $true ("3 个名字都在 ls -l 里: " + (($lines | ForEach-Object { $_.Trim() }) -join ' ;; '))
} else {
    Write-Result 'T5 remote-files-listed' $false ("ls -l 里缺: " + ($miss5 -join ', ') + "（已找到: " + (($hit5 -join ', ') -replace '^\s*$', '无') + "）")
}

# ---------------------------------------------------------------- T7: MessageBox 结果文案
Write-Host ''
Write-Host '--- [T7] 静默模式结束后的 MessageBox（标题 + 结果文案） ---'
$t7Ok = $false
$t7Detail = ''
$t7Proc = $null
$t7TimeoutSec = 90

if (-not $winHelperOk) {
    $t7Detail = 'Win32 助手不可用，无法枚举窗口'
} elseif (-not $exeExists) {
    $t7Detail = 'Lingxi.exe 不存在'
} elseif (-not $fixtureOk) {
    $t7Detail = '测试夹具没建好，跳过'
} else {
    try {
        $t7Proc = Start-Process -FilePath $exe -ArgumentList $argStr -PassThru
    } catch {
        $t7Detail = 'Start-Process 失败: ' + $_.Exception.Message
    }

    if ($null -ne $t7Proc) {
        $t7Deadline = (Get-Date).AddSeconds($t7TimeoutSec)
        $dlg = [IntPtr]::Zero
        $dlgTitle = ''
        $childText = ''

        while ((Get-Date) -lt $t7Deadline) {
            Start-Sleep -Milliseconds 400
            $handles = @()
            try { $handles = [HTVerifyWin]::TopWindowsOfPid($t7Proc.Id) } catch { $handles = @() }
            foreach ($h in $handles) {
                if (-not [HTVerifyWin]::Visible($h)) { continue }
                $ttl = [HTVerifyWin]::TextOf($h)
                if ($null -ne $ttl -and $ttl.Contains('灵犀')) {
                    $dlg = $h
                    $dlgTitle = $ttl
                    break
                }
            }
            if ($dlg -ne [IntPtr]::Zero) { break }
            if ($t7Proc.HasExited) { break }
        }

        if ($dlg -eq [IntPtr]::Zero) {
            $t7Detail = ("{0}s 内没等到标题含「灵犀」的窗口；进程已退出={1}" -f $t7TimeoutSec, $t7Proc.HasExited)
        } else {
            # 文案可能晚一拍渲染，最多再等 3s 直到 Static 非空
            $textDeadline = (Get-Date).AddSeconds(3)
            do {
                try { $childText = [HTVerifyWin]::ChildText($dlg) } catch { $childText = '' }
                if ($null -ne $childText -and $childText.Trim().Length -gt 0) { break }
                Start-Sleep -Milliseconds 300
            } while ((Get-Date) -lt $textDeadline)

            $hasSuccess = ($null -ne $childText -and $childText.Contains('成功'))
            if ($hasSuccess) {
                $t7Ok = $true
                $t7Detail = ("窗口标题=`"{0}`"；文案: {1}" -f $dlgTitle, ($childText.Trim() -replace '\s*\|\s*$', ''))
            } else {
                $t7Detail = ("窗口标题=`"{0}`" 但文案里没有「成功」: {1}" -f $dlgTitle, (($childText.Trim()) -replace '^$', '(空)'))
            }

            # 关掉 MessageBox（WM_CLOSE），让 winexe 自己退
            try { $null = [HTVerifyWin]::Close($dlg) } catch { }
            $exitDeadline = (Get-Date).AddSeconds(10)
            while ((Get-Date) -lt $exitDeadline -and -not $t7Proc.HasExited) { Start-Sleep -Milliseconds 300 }
            if (-not $t7Proc.HasExited) {
                Write-Info 'WM_CLOSE 后 10s 进程未退出，强制 Stop-Process'
            }
        }

        try {
            if (-not $t7Proc.HasExited) { Stop-Process -Id $t7Proc.Id -Force -ErrorAction SilentlyContinue }
        } catch { }
        Start-Sleep -Milliseconds 300
    }
}
Write-Result 'T7 messagebox-result-text' $t7Ok $t7Detail

# ---------------------------------------------------------------- T6: 清理
Write-Host ''
Write-Host '--- [T6] 清理设备侧 + 本地临时文件 ---'
$t6Ok = $false
$t6Detail = ''
try {
    Remove-RemoteItems -Names $expectedNames
    Start-Sleep -Milliseconds 300
    $afterNames = Get-RemoteNames -TimeoutSec 30
    $leftover = @($expectedNames | Where-Object { $afterNames -contains $_ })

    if ($null -ne $localDir -and (Test-Path -LiteralPath $localDir)) {
        Remove-Item -LiteralPath $localDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    $localGone = -not (Test-Path -LiteralPath $localDir)

    if ($leftover.Count -eq 0 -and $localGone) {
        $t6Ok = $true
        $t6Detail = '设备侧 3 项已删净，本地临时目录已删'
    } else {
        $t6Detail = ("设备侧残留: " + (($leftover -join ', ') -replace '^\s*$', '无') + "；本地目录已删: " + $localGone)
    }
} catch {
    $t6Detail = '清理异常: ' + $_.Exception.Message
}
Write-Result 'T6 cleanup' $t6Ok $t6Detail

# ---------------------------------------------------------------- 汇总
Write-Host ''
Write-Host ('=' * 78)
Write-Host ("汇总: PASS={0} FAIL={1}" -f $script:Pass, $script:Fail)
Write-Host ('=' * 78)

if ($script:Fail -gt 0) { exit 1 }
exit 0
