# 从源码构建

本项目刻意做得很朴素：没有解决方案文件、没有项目文件、没有 NuGet 包、不依赖 Visual Studio。全部构建就是一个 `csc.exe` 命令行。

---

## 依赖

| 依赖 | 要求 | 说明 |
|---|---|---|
| 操作系统 | Windows | WinForms，无跨平台可能 |
| 编译器 | .NET Framework 4.0 的 `csc.exe` | **Windows 10 / 11 系统自带，无需另装** |
| `hdc.exe` | 可选 | **仅运行程序时需要，编译不需要** |
| Visual Studio | 不需要 | |
| NuGet | 不需要 | 零第三方依赖 |

### 关于编译器

`csc.exe` 是 .NET Framework 自带的 C# 编译器，路径固定：

```
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe      (64 位)
C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe        (32 位)
```

`build.cmd` 会先找 64 位那个，找不到再退回 32 位。Windows 10/11 默认都带着，**不需要安装任何 SDK**。

需要引用的框架程序集只有两个，同样系统自带：

```
System.Windows.Forms.dll
System.Drawing.dll
```

### 关于 hdc.exe（三种获取方式）

编译**不需要** `hdc.exe`，但运行需要。程序自己按以下顺序查找（见 `src/HdcCore.cs` 的 `HdcPath` 属性）：

1. **程序同目录的 `hdc.exe`** —— 最省事；
2. 环境变量 **`LINGXI_HDC`** 指向的完整路径；
3. **`PATH`** 里的 `hdc.exe`；
4. **DevEco Studio 默认安装路径**：
   `%ProgramFiles%\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe`
   （也会扫描 `sdk\` 下的其他版本目录，取找到的第一个）

获取方式三选一：

- **从 DevEco Studio 复制**（最常用）：装好 DevEco Studio 后，到
  `sdk\default\openharmony\toolchains\` 下把 `hdc.exe` 复制到 `Lingxi.exe` 旁边。
- **设置环境变量**：`setx LINGXI_HDC "D:\path\to\hdc.exe"`，之后无需复制文件。
- **加入 PATH**：把 `hdc.exe` 所在目录加进系统 `PATH`。

> 本仓库**不打包 `hdc.exe`**。它属于华为 DevEco Studio 的一部分，跟随其自身许可分发，不适合转授权塞进本仓库。`.gitignore` 已把它排除。

---

## 一键编译

在资源管理器里**双击 `build.cmd`**。

它做的事：

1. 定位 `csc.exe`（64 位优先，回退 32 位），找不到就报错退出；
2. 带上图标与清单资源编译四个源文件；
3. 输出 `Lingxi.exe` 到仓库根目录；
4. 打印 `BUILD OK` 或 `BUILD FAILED`。

注意 `build.cmd` 里的资源路径是 **`%~dp0res\app.ico`** 和 **`%~dp0res\app.manifest`** —— 资源在 `res/` 子目录里，不在根目录。如果写成 `%~dp0app.ico` 会得到：

```
error CS1926: 读取 Win32 清单文件"…\app.manifest"时出错 -- 系统找不到指定的文件
error CS1567: 生成 Win32 资源时出错: 读取图标"…\app.ico"时出错
```

`%~dp0` 是批处理里「本脚本所在目录（带尾部反斜杠）」的展开，所以脚本从哪个目录被调用都不影响。

---

## 手工编译

在仓库根目录下打开 `cmd.exe`：

```bat
cd src
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /out:../Lingxi.exe /win32icon:../res/app.ico /win32manifest:../res/app.manifest HdcCore.cs Program.cs MainForm.cs Fluent.cs
```

各参数含义：

| 参数 | 作用 |
|---|---|
| `/nologo` | 不打印编译器版权横幅 |
| `/target:winexe` | 生成 Windows 图形程序（不是控制台程序，不带黑框） |
| `/out:../Lingxi.exe` | 产物路径，输出到仓库根目录 |
| `/win32icon:../res/app.ico` | 嵌入图标（9 档 16–256 像素） |
| `/win32manifest:../res/app.manifest` | 嵌入清单（声明 `dpiAware=true`） |

源文件四个，顺序无所谓：

- `HdcCore.cs` —— 核心逻辑
- `Program.cs` —— 入口
- `MainForm.cs` —— 界面
- `Fluent.cs` —— 自绘控件与 DPI 缩放

### 关于 `/r:` 引用

`System.Windows.Forms.dll` 和 `System.Drawing.dll` 通常会被 csc **自动解析**（它们在 .NET Framework 的默认引用集里），所以上面的命令没写 `/r:` 也能编过。

但如果你装了多个框架版本、或者遇到 `CS0246: 未能找到类型或命名空间名称`，显式加上更保险：

```bat
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /out:../Lingxi.exe /win32icon:../res/app.ico /win32manifest:../res/app.manifest /r:System.Windows.Forms.dll /r:System.Drawing.dll HdcCore.cs Program.cs MainForm.cs Fluent.cs
```

### 预期产物

```
Lingxi.exe    124,416 字节（约 124 KB）
```

这是实测产物大小。体积这么小是因为完全零第三方依赖。如果产物显著大于这个数，检查是不是误引入了调试符号或多余资源；如果显著小于（比如不到 100 KB），说明资源没嵌进去或编译没真正成功 —— CI 里就有一道 `> 100 KB` 的检查。

---

## 在 Git Bash / MSYS 里编译

**不要直接照抄上面的命令。** MSYS 会重写路径参数，导致两种互相矛盾的报错：

- `src/HdcCore.cs` → `error CS1504`（找不到文件）
- `src\HdcCore.cs` → `error CS2001`（反斜杠被吃掉，变成 `srcHdcCore.cs`）

必须先关掉路径转换，并 `cd` 到 `src` 用裸文件名：

```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
cd src
"C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe" \
  /nologo /target:winexe /out:../Lingxi.exe \
  /win32icon:../res/app.ico /win32manifest:../res/app.manifest \
  HdcCore.cs Program.cs MainForm.cs Fluent.cs
```

原理详见 [PITFALLS.md 第 5 条](PITFALLS.md#5-git-bash--msys-会重写-windows-路径)。

---

## 运行测试

测试需要**真机**：鸿蒙设备已用数据线连接，且 USB 调试已打开并授权。三个脚本都用 PowerShell 5.1（Windows 自带）。

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-recv.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify-gui2.ps1
```

| 脚本 | 覆盖 | 真机结果 |
|---|---|---|
| `verify.ps1` | 7 项：exe 存在 / `hdc -v` / 设备在线 / 静默传输 / 远程落盘核对 / MessageBox 文案 / 清理 | `PASS=7 FAIL=0` |
| `verify-recv.ps1` | 8 项：中文名、目录递归拉取、覆盖写入、缺文件失败路径等 | `PASS=8 FAIL=0` |
| `verify-gui2.ps1` | 拖拽接线与分栏拖动 | 通过 |

脚本支持传参覆盖默认值：

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tests\verify.ps1 -Root "D:\path\to\lingxi" -Serial <设备序列号>
```

> ⚠️ 这些 `.ps1` 文件**必须保存为 UTF-8 带 BOM**。否则 PowerShell 5.1 会按 GB2312 解码，中文字面量静默变乱码且不报错。详见 [PITFALLS.md 第 4 条](PITFALLS.md)。

### 在 CI 里

CI 没有真机、也没有 `hdc.exe`，所以**只验证能编译通过**，不跑上面这些脚本。见 `.github/workflows/build.yml`。

---

## 常见编译错误

| 错误 | 原因 | 修法 |
|---|---|---|
| `CS1926` 读取 Win32 清单文件出错 | `/win32manifest:` 指向的路径不对 | 资源在 `res/` 下，用 `../res/app.manifest` |
| `CS1567` 生成 Win32 资源时出错 | `/win32icon:` 路径不对 | 用 `../res/app.ico` |
| `CS1504` 源文件未能打开 | Git Bash 路径被改写 | 见上文 Git Bash 一节 |
| `CS2001` 未能找到源文件 | 反斜杠被吃掉 | 用 `/` 或 `cd src` 后用裸文件名 |
| `CS0246` 未能找到类型或命名空间名称 | 缺 `/r:System.Windows.Forms.dll` | 显式加 `/r:` 引用 |
| `CS1525` / `CS1026` 之类的语法错 | 用了 C# 6+ 语法 | 见下方语言子集限制 |
| `CS1617` 语言版本无效 | 手动指定了 `/langversion` | 去掉该参数，默认就是 C# 5 |

---

## 语言子集限制（重要）

编译器是 .NET Framework 4.0 的 `csc.exe`，它只支持 **C# 5**。以下语法**一律不能用**，用了就编译失败：

- `$"..."` 字符串插值（C# 6）
- `?.` / `?[` 空传播运算符（C# 6）
- `nameof(...)`（C# 6）
- 表达式体成员 `=>`（C# 6）
- 自动属性初始化器 `{ get; set; } = x;`（C# 6）
- `out var`（C# 7）
- 元组 `(a, b)` 与解构（C# 7）
- 模式匹配 `is Type x`（C# 7）
- 局部函数（C# 7）

**可以用的**：`string.Format`、`var`、LINQ、泛型、lambda、`async/await`（C# 5 就有，但 WinForms 里用后台线程更稳）。

写新代码时如果习惯性敲了 `$"..."`，编译器会在那一行报 `CS1056: 意外的字符"$"`。改成 `string.Format("{0}", x)` 即可。

---

## 修改清单（改代码时容易忘的地方）

- **改像素尺寸**：所有常量以 96 DPI 为基准书写，落屏时统一过 `Fluent.S()` / `Fluent.SF()`。新加的固定像素**不要忘了包一层**，否则在高 DPI 下尺寸不对。
- **改随 DPI 变化的尺寸**：必须是**属性**，不能是 `const`（`const` 编译期固化，运行时改不了）。参考 `FluentTabs.StripHeightPx`。
- **改成功判定**：绝不能依赖 hdc 的退出码，只能看 stdout 有没有 `FileTransfer finish, Size:`。[PITFALLS.md 第 3 条](PITFALLS.md)。
- **加资源文件**：记得同时更新 `build.cmd` 与 CI workflow 里的资源路径。
- **改 `.ps1`**：保存时确认带 UTF-8 BOM。
