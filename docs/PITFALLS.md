# 踩坑手册

这份文档记录的每一条都在真机上踩过、验证过，并给出了**判据**（怎么确认自己遇到了）和**修法**（怎么解决）。阅读顺序建议按编号，前两条最容易把人卡住，而且它们的表现都极具误导性。

---

## 1. 目录完整性级别（Low IL）会让 hdc 静默罢工

### 表现

程序启动后只显示「未找到鸿蒙设备」，但设备明明插着线、USB 调试也开了、用 DevEco Studio 却能看到设备。

手工执行 `hdc list targets`：

```
（空输出）
```

**返回空、退出码 0、零报错。** 什么都提示不出来。

更迷惑的是同一个 Low IL 还会引发第二个症状：**双击 `Lingxi.exe` 弹出「无法验证发布者」。**

### 原因

程序所在目录（或其某一级父目录）继承了 `Low Mandatory Level` 完整性级别。Windows 的强制完整性控制（MIC）不允许低完整性进程向中完整性位置写入。于是 hdc 的后台服务写不了自己的 PID 文件：

```
%TEMP%\.HDCServer.pid
```

写失败之后 hdc 不报错 —— 它只是启动不了后台服务，然后 `list targets` 老老实实返回一个空列表，退出码 0。上层代码没有任何可依据的失败信号，只能显示「未找到设备」。

第二条症状同理：exe 位于低完整性目录时，Windows 认为它不是可信来源，于是弹出发布者验证警告。**这不是 SmartScreen，不是 MOTW（网络下载标记），也不是缺少代码签名 —— 你去买证书签名也压不住。** 很多人在签名上白花钱，根因在这。

### 判据

```bat
icacls "目录路径" | findstr Mandatory
```

如果输出里出现 `Low Mandatory Level`，就是它。正常应该是 `Medium Mandatory Level`。

例如：

```bat
icacls "C:\path\to\your\lingxi" | findstr Mandatory
```

### 修法

把该目录（含子项）的完整性级别设为 Medium：

```bat
icacls "目录路径" /setintegritylevel "(OI)(CI)Medium"
```

`(OI)` = 对象继承，`(CI)` = 容器继承，两者合起来表示对现有子项和以后新建的子项都生效。改完重开一个命令行窗口再试（hdc 的后台服务可能已经带着旧状态在跑，必要时 `hdc kill` 再 `hdc start`）。

### 补充

- 这条在把程序放在**桌面**、**下载**、或者某些被安全策略标记过的目录时尤其常见。
- 换一个目录（比如 `D:\tools\lingxi\`）也常常能绕过，但根治还是改完整性级别。

---

## 2. 150% DPI 缩放 + 无 DPI 感知 = 全部文字被位图拉伸变糊

### 表现

在 125% / 150% / 175% 缩放的系统上，程序里所有文字都是模糊的，笔画发虚，小字几乎不可读。用截图工具放大看，会发现字体边缘有明显的插值痕迹 —— 整张界面像是被当成一张图片拉伸过。

### 原因

程序没有向 Windows 声明自己支持高 DPI。Windows 于是把它当作「DPI 不感知」的老程序，先按 96 DPI 渲染好一整张位图，再把这张位图放大到实际的 144 DPI（150%）。字体是位图的一部分，自然一起被插值拉伸，于是糊成一团。

### 判据

```csharp
GetDpiForSystem()  // 返回 144 表示当前是 150% 缩放
```

或者看注册表 / 显示设置里的缩放比例。系统 DPI 为 96 时是 100%，120 是 125%，144 是 150%，192 是 200%。

### 修法

**第一步：在 manifest 里声明 DPI 感知。** 本项目在 `res/app.manifest` 里写的是：

```xml
<application xmlns="urn:schemas-microsoft-com:asm.v3">
  <windowsSettings>
    <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true</dpiAware>
  </windowsSettings>
</application>
```

声明之后 Windows 不再拉伸位图，字体按真实像素渲染 —— 字清楚了。

**第二步（关键，漏了会更糟）：代码里所有固定像素必须手动按 DPI 缩放。**

声明感知之后，坐标和尺寸的单位仍然是「像素」，但 96 DPI 下设计好的 8px 间距在 144 DPI 下只有视觉上的 5.3px —— 界面会**缩成一小块**，比糊更难看。所以所有像素常量都要过一遍缩放层：

```csharp
Fluent.S(float px)   // 按 dpi/96 缩放，四舍五入取整，最小返回 1
Fluent.SF(float px)  // 保留小数，用于画笔宽度
Fluent.InitDpi()     // 扫描系统 DPI 设定 Scale，必须在创建任何控件之前调用
```

本项目全程序的像素常量都以 96 DPI 为基准书写，落屏时统一乘 `Fluent.Scale`。这样字体（pt，天然随 DPI）与控件尺寸（px，手动缩放）比例一致，不会出现「字大框小」。

### 陷阱：`const int` 不能随 DPI 变

如果一个尺寸被写成 `const`，它就在编译期固化了，运行时无法跟着 DPI 变化。本项目在 `FluentTabs` 上踩过这个坑 —— 标签条高度原本是 `const int StripHeight`，导致 150% 下标签条不跟着长高，文字被切掉。

后来改成了属性：

```csharp
public static int StripHeightPx   // 每次读取时按 DPI 重新计算
```

**规则：任何随 DPI 变化的尺寸都必须是属性（或者方法调用），不能是 `const`。**

---

## 3. hdc 失败时退出码也可能是 0

### 表现

传输实际上失败了，但代码里判断 `process.ExitCode == 0`，于是当成成功，给用户显示「已成功发送」。

### 原因

hdc 的退出码并不可靠。它在多种失败情形下（尤其是前面第 1 条说的服务启动失败、以及部分传输错误）都会返回 0。**退出码 0 不等于成功。**

### 判据

成败**只能**靠 stdout 里有没有这一行：

```
FileTransfer finish, Size:N, File count = N, time:Xms rate:YkB/s
```

具体判据是包含 `FileTransfer finish, Size:`。

### 修法

```csharp
// 错：只看退出码
if (proc.ExitCode == 0) { /* 认为成功 */ }

// 对：看 stdout 里有没有完成标记
bool ok = stdout.Contains("FileTransfer finish, Size:");
```

本项目在 `src/HdcCore.cs` 的 `Send` / `Pull` 里都按这个规则判定，没有一处依赖退出码。

同理，**列目录失败**也不能靠退出码 —— 空输出和「目录真的为空」在退出码上完全无法区分。

---

## 4. PowerShell 5.1 脚本含中文必须带 UTF-8 BOM

### 表现

脚本本身语法正确，但在中文 Windows 上跑起来，所有中文字面量都变成乱码。更糟的是这类错误**不抛异常**：脚本照常执行，只是比较永远不成立。

典型例子：

```powershell
# 期望按标题找到窗口，实际恒返回 0
$h = FindWindow($null, '灵犀 · 成功')
```

因为字面量已经变成乱码，自然匹配不上任何窗口标题。你会花很久怀疑 `FindWindow`、怀疑窗口真的没弹出，其实只是编码问题。

### 原因

PowerShell 5.1 读取 `.ps1` 文件时，**如果没有 BOM，就按系统的 ANSI 代码页解码**（简体中文系统是 GB2312 / CP936），而不是 UTF-8。UTF-8 编码的中文字节被当成 GB2312 解释，于是变成乱码。PowerShell 7+ 默认 UTF-8，不受影响，但 Windows 10/11 自带的是 5.1。

### 判据

用十六进制看文件开头有没有 `EF BB BF`：

```bat
certutil -dump tests\verify.ps1 | findstr /i "BOM"
```

或者用 PowerShell 检查：

```powershell
$bytes = [System.IO.File]::ReadAllBytes('tests\verify.ps1')
'{0:X2} {1:X2} {2:X2}' -f $bytes[0], $bytes[1], $bytes[2]   # 应为 EF BB BF
```

### 修法

把文件另存为「UTF-8 带 BOM」。注意：很多编辑器的「UTF-8」选项是**不带 BOM** 的，要选明确的「UTF-8 with BOM」。

### 更好的做法：弹窗检测别用中文 `FindWindow`

即使编码问题解决了，靠中文标题找窗口也很脆弱（标题里还有 `·` 这种字符，容易被各种环节改写）。检测 MessageBox 请改用窗口类名：

```powershell
# 用 EnumWindows 遍历顶层窗口
# 条件：GetClassNameW(handle) == "#32770"  (标准对话框类名)
#   且 IsWindowVisible(handle)             (确实显示出来了)
```

`#32770` 是 Win32 对话框的系统类名，与语言、标题内容都无关，稳定得多。

**本项目 `tests/` 下的脚本一律按这个方式判定弹窗。**

> 另注：`tests/verify-gui2.ps1` 通篇不含非 ASCII 字符，就是为了彻底避开这个问题。

---

## 5. Git Bash / MSYS 会重写 Windows 路径

### 表现

在 Git Bash 里编译，报一堆莫名其妙的错：

```
error CS1504: 源文件 "src/HdcCore.cs" 未能打开
error CS2001: 未能找到源文件 "srcHdcCore.cs"
```

同一个命令在 `cmd.exe` 里跑完全正常。

### 原因

MSYS / Git Bash 在把参数交给 Windows 原生程序（这里是 `csc.exe`）之前，会做一层「路径转换」：

- 以 `/` 开头的参数（如 `/nologo`、`/target:winexe`）被误认为 Unix 绝对路径，改写成 `C:/Program Files/Git/nologo` 之类；
- 反斜杠 `\` 在某些位置被当成转义字符吃掉。

于是出现了两个方向都错的诡异现象：

| 写法 | 实际传给 csc 的 | 错误 |
|---|---|---|
| `src/HdcCore.cs` | 被路径转换改写 | `CS1504`（找不到文件） |
| `src\HdcCore.cs` | 反斜杠被吃掉 → `srcHdcCore.cs` | `CS2001`（源文件不存在） |

### 修法

**关掉路径转换，并 `cd` 到 `src` 目录再编译。**

```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
cd src
"C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe" \
  /nologo /target:winexe /out:../Lingxi.exe \
  /win32icon:../res/app.ico /win32manifest:../res/app.manifest \
  HdcCore.cs Program.cs MainForm.cs Fluent.cs
```

要点：

- `MSYS_NO_PATHCONV=1` 关掉 Git Bash 的路径改写；
- `MSYS2_ARG_CONV_EXCL='*'` 关掉 MSYS2 的参数改写（两者针对不同实现，都设上最稳）；
- **先 `cd src` 再用裸文件名**，就完全绕开了「路径分隔符到底该怎么写」这个问题。这也是 `build.cmd` 采用的做法 —— 它把 `src` 路径拼出来单独传，而不是在 `src` 里跑。
- 反斜杠在 bash 里还是转义字符，真要写路径请用 `/` 或双写 `\\`。

**在 `cmd.exe` 里编译则没有这些问题**，`build.cmd` 就是给 `cmd.exe` 用的。

---

## 附加坑

### 6. hdc 拉整个目录会在目标下再套一层同名目录

```
hdc file recv /远程/父目录/子目录  D:\本地目标
```

结果不是 `D:\本地目标\文件...`，而是：

```
D:\本地目标\子目录\文件...
```

**多套了一层 `子目录`。** 传目录方向同理：`hdc file send <本地目录> <远程父目录>` 会在远程父目录下**新建同名目录**。

本项目在 `Pull` 里的处理方式是：先把每个条目收到 `<localDir>\.htmp\`，成功后再移到 `<localDir>\<name>`，从而把这一层嵌套「拍平」。失败则保留 tmp 供排查。

另外注意：**目标本地目录不存在时，单文件接收会把「目录」当文件名用**（写出一个同名文件），所以本地目录必须先建好。

### 7. `/data/local/tmp/` 下的测试夹具会被系统定期清理

调试时喜欢把测试文件放在 `/data/local/tmp/` —— 它可写、不用权限。但系统会定期清理这个目录，你的夹具可能在测试跑到一半时消失，表现为「刚才还在的文件突然找不到了」。

**用唯一前缀命名**（例如 `/data/local/tmp/_lx_<时间戳>_...`），并且每个测试脚本在开头和结尾都做一次清理，这样：

- 不会被别的测试或历史残留干扰；
- 被系统清理掉时，从名字上一眼能看出是本项目的临时文件；
- 重复运行是幂等的。

---

## 快速排查清单

遇到问题时按这个顺序过一遍：

| 现象 | 先查什么 |
|---|---|
| 「未找到鸿蒙设备」 | ① USB 调试开了吗、授权了吗 ② `icacls 目录 \| findstr Mandatory`（第 1 条） ③ `hdc list targets` 手工跑一次 |
| 双击 exe 弹「无法验证发布者」 | 目录完整性级别（第 1 条），**不是签名问题** |
| 界面文字发糊 | DPI 感知声明 + `Fluent.S()` 缩放（第 2 条） |
| 界面缩成一小块 | 声明了 DPI 感知但没做像素缩放（第 2 条第二步） |
| 显示成功但其实没传过去 | 是不是在用退出码判成败（第 3 条） |
| PowerShell 脚本中文全乱 | 有没有 UTF-8 BOM（第 4 条） |
| 弹窗检测恒失败 | 改用 `#32770` 类名（第 4 条） |
| Git Bash 里编译报 CS1504 / CS2001 | `MSYS_NO_PATHCONV=1` + `cd src`（第 5 条） |
| 拉下来的目录多套了一层 | hdc 的固有行为（第 6 条） |
| 测试夹具莫名消失 | `/data/local/tmp/` 被清理（第 7 条） |
