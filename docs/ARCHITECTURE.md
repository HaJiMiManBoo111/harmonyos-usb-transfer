# 灵犀 (Lingxi) 接口契约与设计说明

Windows ↔ 鸿蒙（HarmonyOS PC）双向传文件工具的设计契约与实测结论。本文档记录**实现约束与设备行为实测结果**，改源码前先读一遍。

## 硬性技术约束（违反即编译失败）

1. **只能用 C# 5**。编译器是 `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe`（.NET Framework 4）。
   - 禁止：`$"..."` 字符串插值、`?.` 空传播、`nameof`、表达式体成员 `=>`、自动属性初始化器、`out var`、元组、模式匹配。
   - 允许：`string.Format`、`var`、LINQ、泛型、lambda。WinForms 里用后台线程而不是 `async/await`（`UiInvoke` 统一 marshal 回 UI 线程）。
2. 目标框架引用：`/r:System.Windows.Forms.dll /r:System.Drawing.dll`。
3. 编译命令（Git Bash 里跑必须先 `export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'`，否则 `/nologo` 被改写成路径；且**必须 `cd` 到 `src` 目录再编译**）：
   ```
   csc.exe /nologo /target:winexe /out:../Lingxi.exe /win32icon:../res/app.ico /win32manifest:../res/app.manifest /r:System.Windows.Forms.dll /r:System.Drawing.dll HdcCore.cs Program.cs MainForm.cs Fluent.cs
   ```
   简单起见直接跑仓库根的 `build.cmd`，它会自动找 csc 并带上图标与 manifest。

## 源码结构

| 文件 | 职责 |
|---|---|
| `src/HdcCore.cs` | 核心逻辑：设备解析、`Send` / `Pull` / `ListRemote` / `RemoteDirSize`、`hdc.exe` 多路径查找 |
| `src/Program.cs` | 入口：无参进 GUI，带参走静默模式（`--list` / `--pull`） |
| `src/MainForm.cs` | 全部界面与布局（手算 `SetBounds`，无设计师文件） |
| `src/Fluent.cs` | 自绘控件库（`FluentButton` / `FlatProgress` / `CardPanel` / `StatusPill` / `FluentTabs` / `GripBar`）、DPI 缩放层 `Fluent.S()`、速率计 `SpeedMeter` |
| `res/app.ico`、`res/app.manifest` | 图标（9 档）与 DPI 感知声明 |
| `tests/verify*.ps1` | 真机验收脚本 |

---

# 发送方向：Windows → 鸿蒙

## 已实测的设备事实（不要重新探索，直接用）

- hdc 可执行：程序按序查找 ① exe 同目录 `hdc.exe` ② 环境变量 `LINGXI_HDC` ③ `PATH` ④ DevEco Studio 默认安装路径。实测 `Ver: 3.2.0f`，5.7MB，可脱离 DevEco 独立运行。
- `hdc list targets -v` 输出形如：
  ```
  <serial>\t\tUSB\tConnected\tlocalhost\thdc
  COMn\t\tUART\tReady
  ```
  匹配 USB 的正则：`^\s*(\S+)\s+USB\s+Connected`
- 可写目标目录（全部已实测可写）：
  - 桌面 `/storage/media/100/local/files/Docs/Desktop`  ← **本工具的默认目标**
  - 下载 `/storage/media/100/local/files/Docs/Download`
  - 文档 `/storage/media/100/local/files/Docs/Documents`
  - 图片 `/storage/media/100/local/files/Docs/Images`
  - 视频 `/storage/media/100/local/files/Docs/Videos`
  - 音乐 `/storage/media/100/local/files/Docs/Music`
- 传文件：`hdc -t <sn> file send "<本地路径>" "<远程完整路径>"`，成功输出 `FileTransfer finish, Size:N, File count = 1, time:Xms rate:YkB/s`。
- 传目录：`hdc -t <sn> file send "<本地目录>" "<远程父目录>"`，**在远程父目录下新建同名目录**。
- **成败判定只能靠 stdout 含 `FileTransfer finish`**：hdc 失败时退出码也可能是 0。
- **进度可轮询**（已实测）：传输进行中 `hdc shell "stat -c %s <远程路径>"` 返回不断增长的字节数，且与传输并发不阻塞。
- 中文文件名、带空格文件名均已验证可传。
- 速度参考：26–31 MB/s，120MB 约 4.5s。

## 命令行模式（`src/Program.cs`）

```
Lingxi.exe                          无参 → 打开窗口
Lingxi.exe <path> [<path>...]       裸路径 → 静默发送到鸿蒙桌面
Lingxi.exe --pull <remote> [<remote>...] [--to <localDir>]
Lingxi.exe --list <remoteDir>
Lingxi.exe --log <file>             追加把全过程写到该文件
```

输出行（stdout，同时进 `--log` 文件时也写进去）：
```
LIST|D|<name>|<size>
LIST|F|<name>|<size>
LIST|END|<count>
PULL|OK|<message>
PULL|FAIL|<message>
```
`--to` 缺省 = `%USERPROFILE%\Desktop\from-harmony`。退出码：成功 0，失败 1。

> **注意**：`Console.Out` 在被重定向时会用系统 ANSI 代码页（简体中文系统 = GBK）编码，而调用方通常按 UTF-8 读，中文会乱码。`Program.cs` 用 `AttachParentProcess()` 在重定向时把 `Console.Out` / `Console.Error` 换成无 BOM 的 UTF-8 `StreamWriter` 解决。

---

# 接收方向：鸿蒙 → Windows

## 已实测的设备行为（全部真机跑过，勿凭猜）

| 行为 | 实测结果 |
|---|---|
| `hdc file recv <远程文件> <本地目录>` | 落到 `<本地目录>\<原名>`；已存在则**覆盖** |
| `hdc file recv <远程目录> <本地目录>` | **会嵌套**，落到 `<本地目录>\<目录名>\**` |
| 目标本地目录不存在 | 单文件情况下会把「目录」当文件名用（写成同名文件）⇒ **本地目录必须先建好** |
| 远程路径含中文/空格 | 正常，两端都无损 |
| 进度轮询 | 传输中并发读**本地文件大小**单调递增（60MB 实测 9.2M→19.2M→…→62.9M）⇒ 真进度可行 |
| 成功判定 | stdout 含 `FileTransfer finish, Size:N, File count = N`；**失败时退出码也可能是 0** |
| 列目录 | `find '<dir>' -mindepth 1 -maxdepth 1 -type f -printf '%s\t%f\n'` 可用；`-type d -printf '%f\n'` 可用 |
| `-printf %y` | **不支持**（报 `find: bad -printf %y`），判类型只能分两次调用 `-type f` / `-type d` |
| `-printf %T*` 时间 | 同 `%y` 一样不可靠，**不要用时间** |
| 目录总大小 | 不要用 `du -sb`（含目录 inode 与块对齐，8 字节目录报 6888）；改 `find <dir> -type f -printf '%s\n' \| awk '{s+=$1} END {print s+0}'` |
| 隐藏文件 | `find` 默认包含，`ls` 默认不含 ⇒ 列表用 `find` |
| 远程命令里的引号 | `hdc shell` 用**一对双引号**包裹整条命令，命令内部再出现双引号会提前闭合外层 ⇒ 设备端只跑无引号命令，复杂逻辑放 C# 侧 |

## 新增公开 API（`src/HdcCore.cs`）

```csharp
public class RemoteItem
{
    public string Name;       // 纯文件名
    public string FullPath;   // 远程绝对路径
    public bool IsDir;
    public long Size;         // 目录给递归字节数，失败给 0
}

public static class HdcCore
{
    // 已有的直接用，不要重复实现
    public static DeviceInfo ResolveDevice();          // 抛 TransferException
    public static string HdcPath;
    public static string BaseDir;
    public static string FormatBytes(long b);
    public static string[] RemoteRoots;                // 六个标准目录绝对路径

    // 接收方向
    public static RemoteItem[] ListRemote(string remoteDir);
    public static long RemoteDirSize(string remoteDir);           // 失败返回 0
    public static TransferResult Pull(string[] remotePaths, string localDir,
                                      Action<long, long> onProgress,
                                      Action<string> onLog);
}
```

`Pull` 语义：
- `localDir` 不存在则自动逐级 `Directory.CreateDirectory`。
- 每个条目先 recv 到 `<localDir>\.htmp\`，成功后移到 `<localDir>\<name>`（已存在则先删），失败则保留 tmp 供排查。
- `onProgress(done, total)`：单文件 `total` = 远程文件大小，`done` = 本地已落盘字节；
  目录 `total` 为估算值，`done` = tmp 内递归累计，**两者任一为 0 则调用方按不确定进度处理**。
- `TransferResult.Message` 与发送方向同风格，例如
  `已成功收到桌面：3 项，23.3 KB，耗时 0.4 秒（58.2 KB/s）`。

## 界面实现要点（`src/MainForm.cs`）

窗口是 **TabControl 两页**，公共的日志框 / 进度条 / 状态标签放在 Tab 下面共享：

- **第 1 页「发送到鸿蒙」**：`_panelDrop` 拖拽区 / `_lstFiles` 待发送列表 / 四个按钮。
- **第 2 页「从鸿蒙接收」**：
  - `_cmbRemoteDir`（DropDownList，六项：桌面 / 下载 / 文档 / 图片 / 视频 / 音乐，选中即刷新）
  - `_btnRefresh` 刷新
  - `_lvRemote`（`View.Details`，列：类型 / 名称 / 大小）
  - `_lblLocalDir`（显示当前接收目录）+ `_btnOpenLocal`（打开接收文件夹）
  - `_btnPull` / `_btnPullAll`
- 列表刷新走后台线程（`new Thread(delegate(){...})`，`IsBackground=true`），**不要 async/await**。
- 跨线程 UI 更新一律用 `UiInvoke`，内部带 `IsDisposed` 守卫。
- 进度：`onProgress(done,total)` 里 `total > 0` 走确定进度，`total == 0` 走 Marquee。
- 布局全部手算 `SetBounds`（`LayoutChildren()` → `PageArea()` → `LayoutSendPage()` / `LayoutRecvPage()`），不用 Dock/Anchor。
- 列表与日志之间是一条可上下拖动的 `GripBar`，拖动比例只在本次运行内保留。

## UI 技术硬性要求

- **ISO 兼容 C# 5**，见文首约束。
- **DPI 感知必须声明**（`res/app.manifest` 里 `dpiAware=true`）。原因：系统缩放 150%（`AppliedDPI=144`）时，没有声明的进程会被 Windows 把整个窗口位图拉伸 1.5 倍，**所有文字发虚**。
- 声明 DPI 感知后**不能再用固定像素**，所有尺寸/字号都要过 `Fluent.S(px)`。`Fluent.InitDpi()` 在 `Program.Main` 里通过 `Graphics.FromHwnd(IntPtr.Zero).DpiX / 96f` 算出缩放系数。
- 绘制类控件（`FluentTabs`、`StatusPill`、`GripBar`、`FlatProgress`、`FluentButton`）全部自行 `OnPaint`，不依赖系统主题，深色/浅色系统主题下表现一致。

---

# 公开 API 契约（签名不可改）

```csharp
namespace Lingxi
{
    public class DeviceInfo
    {
        public string Serial;       // 例 "ABCDEF0123456789"
        public bool IsUsb;
        public override string ToString(); // 确切格式："鸿蒙设备 (USB) <serial>"
                                          // 网络设备时为 "鸿蒙设备 (网络) <serial>"
    }

    public class TransferResult
    {
        public bool Success;
        public string Message;      // 失败原因或成功摘要，可直接显示给用户
        public int FileCount;
        public long TotalBytes;     // 本次传输的本地总字节数
        public long ElapsedMs;
    }

    public class TransferException : System.Exception
    {
        public TransferException(string message);
    }

    public static class HdcCore
    {
        // 可写目标目录常量
        public const string RemoteDesktop  = "/storage/media/100/local/files/Docs/Desktop";
        public const string RemoteDownload = "/storage/media/100/local/files/Docs/Download";

        // exe 所在目录
        public static string BaseDir { get; }
        // 解析出来的 hdc.exe 完整路径（多路径查找）
        public static string HdcPath { get; }

        // 枚举在线设备；无设备返回空数组（不抛异常）
        public static DeviceInfo[] ListDevices();

        // 取第一个 USB 设备；没有则取第一个任意设备；都没有抛 TransferException。
        // hdc.exe 缺失时也抛 TransferException，消息里明确写「缺少 hdc.exe」，
        // 以此与「设备未连接」区分（UI 直接把 ex.Message 显示出来即可）。
        public static DeviceInfo ResolveDevice();

        // 传一批文件/目录到 remoteDir。
        //   paths      本地路径数组（文件或目录混合）
        //   remoteDir  远程目标目录，用上面的常量
        //   onProgress 可为 null。文件传 determinate 进度：(已传字节, 总字节)；
        //              目录传 indeterminate：(已传字节, 0)，第二个参数传 0 表示不确定。
        //   onLog      可为 null。收到的 hdc 原始输出行，供界面显示。
        // 不抛异常（设备掉线等已转成 Success=false 的 TransferResult），
        // 只有参数非法（空数组、路径不存在）才抛 TransferException。
        public static TransferResult Send(string[] paths, string remoteDir,
                                         System.Action<long, long> onProgress,
                                         System.Action<string> onLog);
    }
}
```

## Program.cs 行为

- 无参数 → `Application.Run(new MainForm())`（winexe，不弹控制台）。
- 有参数 → 静默模式：逐个传 `RemoteDesktop`，结束用 `MessageBox` 报结果，供「发送到」右键菜单使用。

## 验收要点（`tests/` 下的脚本自动跑）

发送方向：

1. `Lingxi.exe` 能双击启动，出现窗口，无控制台黑框。
2. 窗口接受拖拽（至少 1 个文件、含中文名的文件、含空格的路径、一个目录）。
3. 传完后设备 `Desktop` 目录里出现对应文件（用 `hdc shell ls -l` 核对）。
4. 传输中进度条有可见变化（非全程 0 后直接 100%）。
5. 设备拔线时给出明确错误提示，不静默失败、不崩溃。
6. `run.cmd` 双击能启动 exe。

接收方向：

1. `--list` 能列出桌面目录，输出行格式符合上面契约，至少 1 个 `LIST|F|`。
2. 造一个远程测试目录含 2 个文件，`--pull --to <临时本地目录>` 后本地出现对应文件，字节数对得上。
3. 单个远程文件（中文名 + 带空格名）能收到指定本地目录，内容正确。
4. 远程文件 > 30MB 时能观察到进度或最终 `PULL|OK` 耗时合理，不超时。
5. `--to` 指向不存在的新目录时自动创建成功。
6. 拔掉设备 / 传不存在的远程路径时给出 `PULL|FAIL`，退出码 1，不崩溃不卡死。
7. 清理：删远程测试产物、删本地临时目录。

测试脚本必须**有 UTF-8 BOM 或纯 ASCII**（PS 5.1 按系统代码页解码无 BOM 脚本，中文字面量会静默乱码）。

---

# 两个环境坑

## 坑一：目录完整性级别（Low IL）会让 hdc 静默罢工

目录若带 **Low Mandatory Level**（低完整性级别），Windows 对其中任何 exe：

1. **双击一律弹「打开文件 - 安全警告 / 无法验证发布者」**，**与数字签名无关**（实测自签名证书 + 加入 Root/TrustedPublisher 后弹窗照旧；`Get-AuthenticodeSignature` 为 Valid 仍弹）。
2. **`hdc.exe` 起不来后台服务**：它要写 `%TEMP%\.HDCServer.pid`，Low 进程受 no-write-up 限制写不进去 ⇒ `hdc list targets` 返回**空、退出码 0、零报错**，程序只会显示「未找到鸿蒙设备」。
3. **`hdc file recv` 写不了工作区外任何目录**（`[Fail]Error opening file: operation not permitted`）。

对照实验（同一 exe 复制到不同目录，explorer 双击，EnumWindows 数 `#32770`）：
- `%USERPROFILE%\Desktop\` / 其下新建子目录 / `Documents\` / `C:\` → 不弹
- Low IL 树内任意位置 → 0.5–1s 内必弹
- 把文件 `icacls /setintegritylevel Medium` → 不弹；再改回 Low → 又弹

**修法**：
```
icacls "<目录>" /setintegritylevel "(OI)(CI)Medium"
```
目录改了之后，树内所有对象继承为 Medium（实测 60998 个对象，0 个残留 Low）；之后 csc 重新编译出来的新 exe 自动继承 Medium，无需单独处理。签不签名与本问题无关。

判断命令：`icacls "<目录>" | findstr Mandatory`。

排查此问题的可复用探针写法（PS 5.1 注意）：
- 必须**纯 ASCII**，否则无 BOM 的 .ps1 被按系统代码页解码导致中文静默乱码。
- 判弹窗用 `EnumWindows` + `GetClassNameW == "#32770"` + `IsWindowVisible`，不要用中文字面量做 `FindWindow`。
- 判「程序真的起来了」用 `Get-Process | Where-Object { $_.Path -eq $exe }`。

## 坑二：图标缓存与快捷方式

换图标后桌面仍显示旧图标时，按序排查：
1. `Icon.ExtractAssociatedIcon(exe)` / `SHGetFileInfo` 确认 exe 内图标是对的 → 说明问题在缓存。
2. `%LOCALAPPDATA%\Microsoft\Windows\Explorer` 下的 `iconcache_*.db`、`thumbcache_*.db` 时间戳是否比新 exe 旧。
3. 快捷方式的 `IconLocation` 若写成 `<exe>,0`，explorer 对「exe 内图标索引」的缓存最难失效 ⇒ 改成独立 `.ico` 文件的绝对路径。

完整修法：删快捷方式重建（`IconLocation` 指向 `res\app.ico`）→ `Stop-Process -Name explorer -Force` → 删 `iconcache_*` 与 `thumbcache_*` → 重启 explorer。
注意 `Get-ChildItem -Include 'iconcache*'` 不带 `-Recurse` 或通配符路径不生效，要用 `-Path $dir -Filter 'iconcache*'`。
