# 参与贡献

欢迎提 issue 和 pull request。这个项目很小，规则也很简单：**保持小**。

---

## 开始之前

请先读 [docs/PITFALLS.md](docs/PITFALLS.md)。里面记录的每一条都是真机上踩过的坑，其中至少三条会让你的调试方向完全跑偏（比如 `hdc` 返回空列表但退出码是 0，你看代码怎么看都是对的）。

编译与测试步骤见 [docs/BUILD.md](docs/BUILD.md)。

---

## 代码风格

### 语言版本：只能用 C# 5

编译器是 Windows 自带的 .NET Framework 4.0 `csc.exe`，**只支持 C# 5**。这是硬约束，不是偏好。以下语法不能用：

- `$"..."` 字符串插值
- `?.` 空传播
- `nameof(...)`
- 表达式体成员 `=>`
- 自动属性初始化器
- `out var`、元组、模式匹配、局部函数

等价写法用 `string.Format`。提交前请确认 `build.cmd` 能编过。

### 零第三方依赖

不引入 NuGet 包。这不是洁癖 —— 项目能在任何一台 Windows 上用系统自带编译器直接构建，是它存在的价值之一。如果某个功能必须靠外部库，先开 issue 讨论。

### 界面尺寸一律走 DPI 缩放

全程序的像素常量都以 96 DPI 为基准书写，落屏时统一过 `Fluent.S()` / `Fluent.SF()`：

```csharp
int gap = Fluent.S(12);          // 正确
int gap = 12;                    // 错误：150% 缩放下尺寸不对
const int Gap = 12;              // 错误：const 编译期固化，跟不了 DPI
```

**随 DPI 变化的尺寸必须是属性**，不能是 `const`。参考 `FluentTabs.StripHeightPx`。

### 成败判定不看退出码

`hdc` 在失败时也可能返回退出码 0。**唯一可靠的判据是 stdout 里有没有 `FileTransfer finish, Size:`**。任何依赖 `ExitCode` 的判定都要改掉。

### 中文标点

文档正文用中文全角标点，代码块与命令用半角。已有的 `.ps1` 脚本请保持纯 ASCII 或严格带 UTF-8 BOM。

---

## 提交变更

### 分支与提交

- 从 `main` 切分支：`fix/hdc-path`、`feat/batch-rename` 这样。
- 一个 PR 只做一件事。
- 提交信息用中文或英文都行，写清楚**改了什么、为什么**。如果修的是 PITFALLS 里某一条，把编号带上。

### 必须真机验证

涉及传输逻辑的改动，不能只看编译通过 —— 需要一台真实鸿蒙设备，连线并打开 USB 调试。请在 PR 描述里写明测试结果，例如：

```
verify.ps1      PASS=7 FAIL=0
verify-recv.ps1 PASS=8 FAIL=0
```

### 不要提交的东西

`.gitignore` 已经覆盖了，这里再说一遍，因为很容易误传：

- **`hdc.exe`** —— 属于华为 DevEco Studio，跟随其自身许可分发。本仓库不打包、不转授权。
- **`Lingxi.exe`** —— 编译产物，由 CI 和用户本地生成。
- 日志文件、临时测试目录。

---

## 文档变更

- 新踩的坑请补进 `docs/PITFALLS.md`，**必须带判据和修法**。「这里有问题」不算一条。
- 改了构建流程或依赖，同步更新 `docs/BUILD.md` 和 `.github/workflows/build.yml`。
- `docs/ARCHITECTURE.md` 是内部接口契约，改动需谨慎。

---

## 报告问题

提 issue 时请附上：

1. **Windows 版本**与**缩放比例**（设置 → 显示 → 缩放）。
2. **鸿蒙设备型号**与系统版本。
3. 手工执行 `hdc list targets -v` 的输出。
4. **目录完整性级别**：`icacls "程序所在目录" | findstr Mandatory` 的结果 —— 见 PITFALLS 第 1 条，这条经常是根因。
5. `hdc.exe` 是从哪找到的（程序目录 / `LINGXI_HDC` / `PATH` / DevEco Studio）。
6. 如果是命令行模式的问题，加上 `--log 日志.txt` 后附上日志内容。

---

## 许可

提交的代码默认按 [MIT](LICENSE) 授权。
