<#
  灵犀 (Lingxi) 验收脚本 · 接收方向（--list / --pull）
  逐条验证，输出 [PASS]/[FAIL]，最后 PASS=n FAIL=m，有 FAIL 则 exit 1。

  用法（PowerShell 5.1）：
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-recv.ps1
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-recv.ps1 -Root <目录> -Serial <序列号>

  注意：本文件必须保存为 UTF-8 带 BOM，否则 PS 5.1 按 GB2312 解码，中文名会乱码且可能解析失败。
#>
param(
    [string]$Root = '',
    [string]$Serial = ''
)

$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
# hdc 直连时不做 MSYS 路径转换（在 Git Bash 里调用本脚本时也安全）
$env:MSYS_NO_PATHCONV = '1'

# ---- 默认 Root：脚本所在目录（tests）的上一级 ----
if ([string]::IsNullOrEmpty($Root)) {
    $scriptDir = $PSScriptRoot
    if ([string]::IsNullOrEmpty($scriptDir)) {
        try { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path } catch { $scriptDir = '' }
    }
    if ([string]::IsNullOrEmpty($scriptDir)) {
        $scriptDir = (Get-Location).Path
    }
    $Root = Split-Path -Parent $scriptDir
}

$exe  = Join-Path $Root 'Lingxi.exe'

# ---- 定位 hdc.exe：程序同目录 > LINGXI_HDC > PATH > DevEco Studio 默认路径 ----
$hdc = Join-Path $Root 'hdc.exe'
if (-not (Test-Path -LiteralPath $hdc)) {
    if ($env:LINGXI_HDC -and (Test-Path -LiteralPath $env:LINGXI_HDC)) {
        $hdc = $env:LINGXI_HDC
    } else {
        $cmd = Get-Command 'hdc.exe' -ErrorAction SilentlyContinue
        if ($cmd) {
            $hdc = $cmd.Source
        } else {
            $devEco = Join-Path $env:ProgramFiles 'Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
            $hdc = $devEco
        }
    }
}

$RemoteDesktop = '/storage/media/100/local/files/Docs/Desktop'
# 设备 /data/local/tmp 会被系统定期清理，夹具目录名必须带唯一前缀（Lead 实测：_vt/_vd 跑到一半消失）
$RemVt = '/data/local/tmp/_ht_vt'
$RemVd = '/data/local/tmp/_ht_vd'
$CnName  = '中文 测试.txt'
$SpaceName = 'a b.txt'

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

# 单参数加引号（路径含空格/中文时 --to 后面的值必须整体是一个参数）
function Quote-Arg {
    param([string]$s)
    if ($null -eq $s) { return '""' }
    if ($s.Length -eq 0) { return '""' }
    if ($s -match '[\s"]') { return '"' + ($s -replace '"', '\"') + '"' }
    return $s
}

<#
  跑一个进程并抓 stdout/stderr/退出码。
  - 用 ProcessStartInfo 而不是 Start-Process：退出码可靠、编码可控（UTF-8 无 BOM）。
  - 中文参数不受影响，Arguments 是 Unicode 字符串。
#>
function Invoke-Proc {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [int]$TimeoutSec = 120,
        [string]$WorkingDir = ''
    )
    $argStr = (($Arguments | ForEach-Object { Quote-Arg $_ }) -join ' ')
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = $argStr
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $psi.StandardErrorEncoding = New-Object System.Text.UTF8Encoding($false)
    if (-not [string]::IsNullOrEmpty($WorkingDir)) { $psi.WorkingDirectory = $WorkingDir }

    # 必须用 -Property 初始化；对空的 New-Object psobject 赋属性会抛
    # “在此对象上找不到属性 ExitCode”（PS 5.1），那会把每次调用都变成异常。
    $res = New-Object psobject -Property @{
        ExitCode = -1
        Out      = ''
        Err      = ''
        TimedOut = $false
        Args     = $argStr
    }

    $p = $null
    try {
        $p = [System.Diagnostics.Process]::Start($psi)
        $outTask = $p.StandardOutput.ReadToEndAsync()
        $errTask = $p.StandardError.ReadToEndAsync()
        if (-not $p.WaitForExit($TimeoutSec * 1000)) {
            $res.TimedOut = $true
            try { $p.Kill() } catch { }
            try { $p.WaitForExit(5000) | Out-Null } catch { }
        }
        try { $p.WaitForExit() } catch { }
        $res.ExitCode = $p.ExitCode
        try { $res.Out = $outTask.Result } catch { }
        try { $res.Err = $errTask.Result } catch { }
    } catch {
        $res.Err = 'Invoke-Proc 异常: ' + $_.Exception.Message
        $res.TimedOut = $false
    } finally {
        if ($null -ne $p) { try { $p.Dispose() } catch { } }
    }
    return $res
}

# 跑被测程序
function Invoke-App {
    param([string[]]$Arguments, [int]$TimeoutSec = 120)
    return Invoke-Proc -FilePath $exe -Arguments $Arguments -TimeoutSec $TimeoutSec -WorkingDir $Root
}

# 跑 hdc（设备准备/校验用）
function Invoke-Hdc {
    param([string[]]$Arguments, [int]$TimeoutSec = 60)
    return Invoke-Proc -FilePath $hdc -Arguments $Arguments -TimeoutSec $TimeoutSec
}

function Get-OutLines {
    param([string]$Text)
    if ([string]::IsNullOrEmpty($Text)) { return @() }
    return @($Text -split "\r?\n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
}

# 设备在线检查
function Test-DeviceOnline {
    $r = Invoke-Hdc -Arguments @('list', 'targets', '-v') -TimeoutSec 25
    $m = [regex]::Matches([string]$r.Out, '(?m)^\s*(\S+)\s+USB\s+Connected')
    if ($m.Count -gt 0 -and [string]::IsNullOrEmpty($Serial)) {
        $script:Serial = $m[0].Groups[1].Value
        Write-Info ("未指定 -Serial，自动使用第一台 USB 设备: {0}" -f $script:Serial)
    }
    return ($m.Count -gt 0)
}

Write-Host ''
Write-Host ('=' * 78)
Write-Host '灵犀 (Lingxi) 验收 · 接收方向 (tests/verify-recv.ps1)'
Write-Host ('  Root   : ' + $Root)
Write-Host ('  exe    : ' + $exe)
Write-Host ('  hdc    : ' + $hdc)
Write-Host ('  Serial : ' + $Serial)
Write-Host ('  Time   : ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Host ('=' * 78)

# 本地落地目录必须放在「工作区内」（$Root 下面）。
# 原因（本机实测）：hdc.exe / Lingxi.exe 在 %TEMP%、C:\Users\Public、桌面根等
# 工作区外的目录写文件会直接失败 ——
#   [Fail]Error opening file: operation not permitted, path:C:\Users\...\Temp\xxx\a b.txt
# 而写 $Root 内的目录一律成功（FileTransfer finish）。这是验收环境的写盘边界，
# 不是被测程序的 bug；所以落地目录统一取 $Root\tests\_recv_tmp。
$localBase = Join-Path $Root 'tests'
if (-not (Test-Path -LiteralPath $localBase)) { $null = New-Item -ItemType Directory -Path $localBase -Force }
$localDir = Join-Path $localBase '_recv_tmp'

# 幂等预备：先清设备侧与本地上次的残留（清理失败不致命，只是告警）
Write-Host ''
Write-Host '--- [预备] 幂等清理（设备 /data/local/tmp 与本地临时目录） ---'
try {
    $null = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "rm -rf $RemVt $RemVd") -TimeoutSec 30
} catch { }
try {
    if (Test-Path -LiteralPath $localDir) {
        Remove-Item -LiteralPath $localDir -Recurse -Force -ErrorAction SilentlyContinue
    }
} catch { }
try { New-Item -ItemType Directory -Path $localDir -Force | Out-Null } catch { }
Write-Info ("本地落地目录: " + $localDir)

# 设备在线（后续每个用例都依赖；不在线就整体标注，但仍逐条输出）
$devOnline = $false
try { $devOnline = Test-DeviceOnline } catch { $devOnline = $false }
Write-Info ("设备在线: " + $devOnline)

# ---------------------------------------------------------------- T1: exe 存在
Write-Host ''
Write-Host '--- [T1] exe-exists ---'
$exeExists = $false
$exeSize = 0
if (Test-Path -LiteralPath $exe) {
    $exeSize = (Get-Item -LiteralPath $exe).Length
    $exeExists = $exeSize -gt 29000
}
if ($exeExists) {
    Write-Result 'T1 exe-exists' $true ("{0} ({1} bytes > 29000)" -f (Split-Path $exe -Leaf), $exeSize)
} elseif (Test-Path -LiteralPath $exe) {
    Write-Result 'T1 exe-exists' $false ("{0} 只有 {1} bytes（应 > 29000，可能是 v1 旧 exe）" -f (Split-Path $exe -Leaf), $exeSize)
} else {
    Write-Result 'T1 exe-exists' $false ("未找到 {0}" -f $exe)
}

# ---------------------------------------------------------------- T2: --list Desktop
Write-Host ''
Write-Host '--- [T2] list-desktop ---'
$t2Ok = $false
$t2Detail = ''
if (-not $exeExists) {
    $t2Detail = 'exe 不存在或过小，跳过'
} elseif (-not $devOnline) {
    $t2Detail = '设备不在线，跳过'
} else {
    $r2 = Invoke-App -Arguments @('--list', $RemoteDesktop) -TimeoutSec 180
    $l2 = Get-OutLines $r2.Out
    $endLine = ''
    if ($l2.Count -gt 0) { $endLine = $l2[$l2.Count - 1] }
    $endOk = ($endLine -match '^LIST\|END\|\d+$')
    $itemLines = @($l2 | Where-Object { $_ -match '^LIST\|[DF]\|' })
    if ($r2.TimedOut) {
        $t2Detail = ("超时 180s，最后输出: " + $endLine)
    } elseif ($r2.ExitCode -ne 0) {
        $t2Detail = ("退出码 {0}（应为 0）；输出: {1}" -f $r2.ExitCode, (($l2 -join ' ;; ') -replace '^$', '(空)'))
    } elseif (-not $endOk) {
        $t2Detail = ("最后一行不是 LIST|END|<n>，实际: " + ($endLine -replace '^$', '(空)'))
    } elseif ($itemLines.Count -eq 0) {
        $t2Detail = '没有任何 LIST|[DF]| 条目行'
    } else {
        $t2Ok = $true
        $dirCount = @($itemLines | Where-Object { $_ -match '^LIST\|D\|' }).Count
        $fileCount = @($itemLines | Where-Object { $_ -match '^LIST\|F\|' }).Count
        $t2Detail = ("退出码 0，末行 `"{0}`"；条目 {1} 条（D={2} F={3}）" -f $endLine, $itemLines.Count, $dirCount, $fileCount)
    }
}
Write-Result 'T2 list-desktop' $t2Ok $t2Detail

# ---------------------------------------------------------------- 准备 T3 夹具（设备侧）
Write-Host ''
Write-Host '--- [T3] list-chinese（设备侧中文名/空格名） ---'
$t3Ok = $false
$t3Detail = ''
$fixVtOk = $false
$srcCnSize = 0
$srcSpaceSize = 0

if (-not $devOnline) {
    $t3Detail = '设备不在线，跳过'
} elseif (-not $exeExists) {
    $t3Detail = 'exe 不存在或过小，跳过'
} else {
    # /data/local/tmp/_vt 下建两个文件：纯中文名、带空格英文名
    $mk = "rm -rf $RemVt; mkdir -p $RemVt; cd $RemVt; printf 'abc' > '$CnName'; printf 'defgh' > '$SpaceName'; ls -1 $RemVt"
    $rmk = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', $mk) -TimeoutSec 60
    $st = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "stat -c '%s' '$RemVt/$CnName' '$RemVt/$SpaceName' 2>/dev/null") -TimeoutSec 30
    $stLines = Get-OutLines $st.Out
    if ($stLines.Count -ge 2) {
        [void][int64]::TryParse($stLines[0], [ref]$srcCnSize)
        [void][int64]::TryParse($stLines[1], [ref]$srcSpaceSize)
    }
    $fixVtOk = ($srcCnSize -gt 0 -and $srcSpaceSize -gt 0)
    Write-Info ("设备夹具: {0}（{1}） / {2}（{3}）  源大小 {4} / {5} B" -f $CnName, $RemVt, $SpaceName, $RemVt, $srcCnSize, $srcSpaceSize)

    if (-not $fixVtOk) {
        $t3Detail = ("设备侧夹具没建好；mkdir/ls 输出: {0}；stat 输出: {1}" -f (($rmk.Out -split "\r?\n") -join ' / '), (($stLines -join ' / ') -replace '^$', '(空)'))
    } else {
        $r3 = Invoke-App -Arguments @('--list', $RemVt) -TimeoutSec 90
        $out3 = [string]$r3.Out
        $hasCn = $out3.Contains($CnName)
        $hasSpace = $out3.Contains($SpaceName)
        $lines3 = Get-OutLines $out3
        if ($r3.ExitCode -ne 0) {
            $t3Detail = ("退出码 {0}（应为 0）；输出: {1}" -f $r3.ExitCode, (($lines3 -join ' ;; ') -replace '^$', '(空)'))
        } elseif ($hasCn -and $hasSpace) {
            $t3Ok = $true
            $t3Detail = ("输出原样包含两个名字；" + (($lines3 | Where-Object { $_ -match '^LIST\|[DF]\|' }) -join ' ;; '))
        } else {
            $t3Detail = ("名字对不上（中文={0} 空格={1}）；实际输出: {2}" -f `
                    $hasCn, $hasSpace, (($lines3 -join ' ;; ') -replace '^$', '(空)'))
        }
    }
}
Write-Result 'T3 list-chinese' $t3Ok $t3Detail

# ---------------------------------------------------------------- T4: --pull 单文件
Write-Host ''
Write-Host '--- [T4] pull-single ---'
$t4Ok = $false
$t4Detail = ''
$t4LocalName = ''
if (-not $fixVtOk) {
    $t4Detail = '设备夹具没建好（见 T3），跳过'
} else {
    $r4 = Invoke-App -Arguments @('--pull', "$RemVt/$CnName", '--to', $localDir) -TimeoutSec 180
    $lines4 = Get-OutLines $r4.Out
    $hasOk4 = ([string]$r4.Out).Contains('PULL|OK|')
    $t4LocalName = Join-Path $localDir $CnName
    $localExists4 = Test-Path -LiteralPath $t4LocalName
    $localSize4 = 0
    if ($localExists4) { $localSize4 = (Get-Item -LiteralPath $t4LocalName).Length }
    if ($r4.TimedOut) {
        $t4Detail = '超时 180s'
    } elseif ($r4.ExitCode -ne 0) {
        $t4Detail = ("退出码 {0}（应为 0）；输出: {1}；stderr: {2}" -f $r4.ExitCode, (($lines4 -join ' ;; ') -replace '^$', '(空)'), (($r4.Err -replace "\r?\n", ' ').Trim() -replace '^$', '(空)'))
    } elseif (-not $hasOk4) {
        $t4Detail = ("输出不含 PULL|OK|；实际: " + (($lines4 -join ' ;; ') -replace '^$', '(空)'))
    } elseif (-not $localExists4) {
        $t4Detail = ("输出说成功但本地没有文件: " + $t4LocalName)
    } elseif ($localSize4 -ne $srcCnSize) {
        $t4Detail = ("本地字节数 {0} ≠ 源 {1}（{2}）" -f $localSize4, $srcCnSize, $t4LocalName)
    } else {
        $t4Ok = $true
        $t4Detail = ("退出码 0，PULL|OK|；{0} 落地 {1} bytes = 源 {2} bytes" -f $CnName, $localSize4, $srcCnSize)
    }
}
Write-Result 'T4 pull-single' $t4Ok $t4Detail

# ---------------------------------------------------------------- T5: --pull 目录
Write-Host ''
Write-Host '--- [T5] pull-dir ---'
$t5Ok = $false
$t5Detail = ''
$vdInnerNames = @('inner1.txt', 'inner2.txt')
if (-not $devOnline) {
    $t5Detail = '设备不在线，跳过'
} elseif (-not $exeExists) {
    $t5Detail = 'exe 不存在或过小，跳过'
} else {
    $mk5 = "rm -rf $RemVd; mkdir -p $RemVd/sub; cd $RemVd; printf 'dir-root-1' > root1.txt; printf 'dir-sub-1' > sub/inner1.txt; printf 'dir-sub-22' > sub/inner2.txt; ls -1R $RemVd"
    $null = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', $mk5) -TimeoutSec 60
    $chk5 = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "find $RemVd -type f 2>/dev/null") -TimeoutSec 30
    $remoteFiles5 = Get-OutLines $chk5.Out
    Write-Info ("设备侧 {0} 文件: {1}" -f $RemVd, ($remoteFiles5 -join ', '))

    $localVd = Join-Path $localDir (Split-Path $RemVd -Leaf)
    $r5 = Invoke-App -Arguments @('--pull', $RemVd, '--to', $localDir) -TimeoutSec 240
    $lines5 = Get-OutLines $r5.Out
    $hasOk5 = ([string]$r5.Out).Contains('PULL|OK|')
    # 递归找：不锁死层级（hdc 目录拉取可能多一层同名目录）
    $found5 = @()
    $vdDirExists = Test-Path -LiteralPath $localVd
    if (Test-Path -LiteralPath $localDir) {
        $found5 = @(Get-ChildItem -LiteralPath $localDir -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { ($vdInnerNames -contains $_.Name) -or $_.Name -eq 'root1.txt' })
    }
    $foundNames5 = @($found5 | ForEach-Object { $_.Name } | Sort-Object -Unique)
    $allInner = @($vdInnerNames | Where-Object { $foundNames5 -contains $_ })

    if ($r5.TimedOut) {
        $t5Detail = '超时 240s'
    } elseif ($r5.ExitCode -ne 0) {
        $t5Detail = ("退出码 {0}（应为 0）；输出: {1}" -f $r5.ExitCode, (($lines5 -join ' ;; ') -replace '^$', '(空)'))
    } elseif (-not $hasOk5) {
        $t5Detail = ("输出不含 PULL|OK|；实际: " + (($lines5 -join ' ;; ') -replace '^$', '(空)'))
    } elseif (-not $vdDirExists) {
        $t5Detail = ("本地没有 {0} 目录；本地实际: {1}" -f $localVd, ((Get-ChildItem -LiteralPath $localDir -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }) -join ', '))
    } elseif ($allInner.Count -ne 2) {
        $t5Detail = ("递归没找齐 sub 下两个文件；找到: {0}；路径: {1}" -f (($foundNames5 -join ', ') -replace '^$', '(空)'), (($found5 | ForEach-Object { $_.FullName }) -join ' | '))
    } else {
        $t5Ok = $true
        $t5Detail = ("退出码 0，PULL|OK|；{0}\ 结构完整，递归找到: {1}" -f $localVd, ($foundNames5 -join ', '))
    }
}
Write-Result 'T5 pull-dir' $t5Ok $t5Detail

# ---------------------------------------------------------------- T6: --pull 覆盖
Write-Host ''
Write-Host '--- [T6] pull-overwrite ---'
$t6Ok = $false
$t6Detail = ''
if (-not $fixVtOk) {
    $t6Detail = '设备夹具没建好（见 T3），跳过'
} elseif (-not (Test-Path -LiteralPath (Join-Path $localDir $CnName))) {
    $t6Detail = 'T4 没落地，无法测覆盖'
} else {
    # 先确认本地文件存在，再拉一次同一目标
    $beforeSize = (Get-Item -LiteralPath (Join-Path $localDir $CnName)).Length
    $r6 = Invoke-App -Arguments @('--pull', "$RemVt/$CnName", '--to', $localDir) -TimeoutSec 180
    $lines6 = Get-OutLines $r6.Out
    $hasOk6 = ([string]$r6.Out).Contains('PULL|OK|')
    $afterPath = Join-Path $localDir $CnName
    $afterExists = Test-Path -LiteralPath $afterPath
    $afterSize = 0
    if ($afterExists) { $afterSize = (Get-Item -LiteralPath $afterPath).Length }
    if ($r6.TimedOut) {
        $t6Detail = '超时 180s'
    } elseif ($r6.ExitCode -ne 0) {
        $t6Detail = ("第二次退出码 {0}（应为 0，覆盖应成功）；输出: {1}" -f $r6.ExitCode, (($lines6 -join ' ;; ') -replace '^$', '(空)'))
    } elseif (-not $hasOk6) {
        $t6Detail = ("第二次输出不含 PULL|OK|: " + (($lines6 -join ' ;; ') -replace '^$', '(空)'))
    } elseif (-not $afterExists) {
        $t6Detail = '第二次之后本地文件不见了'
    } elseif ($afterSize -ne $srcCnSize -or $afterSize -ne $beforeSize) {
        $t6Detail = ("覆盖后字节数 {0}（源 {1}，覆盖前 {2}）" -f $afterSize, $srcCnSize, $beforeSize)
    } else {
        $t6Ok = $true
        $t6Detail = ("第二次仍退出码 0；字节数 {0} 前后一致且等于源 {1}" -f $afterSize, $srcCnSize)
    }
}
Write-Result 'T6 pull-overwrite' $t6Ok $t6Detail

# ---------------------------------------------------------------- T7: 失败路径
Write-Host ''
Write-Host '--- [T7] pull-fail-missing ---'
$t7Ok = $false
$t7Detail = ''
if (-not $exeExists) {
    $t7Detail = 'exe 不存在或过小，跳过'
} elseif (-not $devOnline) {
    $t7Detail = '设备不在线，跳过'
} else {
    $r7 = Invoke-App -Arguments @('--pull', '/data/local/tmp/_nope_xyz', '--to', $localDir) -TimeoutSec 120
    $lines7 = Get-OutLines $r7.Out
    $hasFail7 = ([string]$r7.Out).Contains('PULL|FAIL|')
    # 反向断言：失败原因必须是「远端路径不存在」，不能是本地写盘/权限问题冒充的失败
    $wrongReason7 = (([string]$r7.Out) -match '访问被拒绝|operation not permitted|无法创建本地目录')
    if ($r7.TimedOut) {
        $t7Detail = '超时 120s（失败路径不该卡住）'
    } elseif ($r7.ExitCode -ne 1) {
        $t7Detail = ("退出码 {0}（应为 1）；输出: {1}" -f $r7.ExitCode, (($lines7 -join ' ;; ') -replace '^$', '(空)'))
    } elseif (-not $hasFail7) {
        $t7Detail = ("退出码对但输出不含 PULL|FAIL|: " + (($lines7 -join ' ;; ') -replace '^$', '(空)'))
    } elseif ($wrongReason7) {
        $t7Detail = ("失败原因不对（是本地写盘/权限问题，不是远端不存在）: " + (($lines7 -join ' ;; ') -replace '^$', '(空)'))
    } else {
        $t7Ok = $true
        $t7Detail = ("退出码 1；输出: " + ($lines7 -join ' ;; '))
    }
}
Write-Result 'T7 pull-fail-missing' $t7Ok $t7Detail

# ---------------------------------------------------------------- T8: 清理
Write-Host ''
Write-Host '--- [T8] cleanup ---'
$t8Ok = $false
$t8Detail = ''
try {
    $null = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "rm -rf $RemVt $RemVd") -TimeoutSec 60
    $chk8 = Invoke-Hdc -Arguments @('-t', $Serial, 'shell', "ls -d $RemVt $RemVd 2>/dev/null; echo DONE") -TimeoutSec 30
    $left8 = @(Get-OutLines $chk8.Out | Where-Object { $_ -ne 'DONE' })

    if (Test-Path -LiteralPath $localDir) {
        Remove-Item -LiteralPath $localDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    $localGone = -not (Test-Path -LiteralPath $localDir)

    if ($left8.Count -eq 0 -and $localGone) {
        $t8Ok = $true
        $t8Detail = ("设备侧 " + (Split-Path $RemVt -Leaf) + "/" + (Split-Path $RemVd -Leaf) + " 已删净，本地临时目录已删")
    } else {
        $t8Detail = ("设备残留: " + (($left8 -join ', ') -replace '^$', '无') + "；本地目录已删: " + $localGone)
    }
} catch {
    $t8Detail = '清理异常: ' + $_.Exception.Message
}
Write-Result 'T8 cleanup' $t8Ok $t8Detail

# ---------------------------------------------------------------- 汇总
Write-Host ''
Write-Host ('=' * 78)
Write-Host ("汇总: PASS={0} FAIL={1}" -f $script:Pass, $script:Fail)
Write-Host ('=' * 78)

if ($script:Fail -gt 0) { exit 1 }
exit 0
