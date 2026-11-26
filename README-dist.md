# Flux 1.27.0-flux

基于上游 **Deskflow 1.27.0** 源码重新编译，把「灵犀」的鸿蒙文件传输能力以源码方式
合并进 Deskflow，并整体更名为 **Flux**。一个程序、一个窗口、两条传文件通道。

## 两条通道

| 通道 | 走什么 | Windows 侧 | 鸿蒙侧 |
|---|---|---|---|
| A · hdc | `hdc.exe file send/recv`，USB 数据线 | File Transfer 页「发送到鸿蒙 / 从鸿蒙拉取」，可浏览远程目录 | 设备开启调试并授权 |
| B · KVM / DFTR | TCP 24800 上的 Barrier 协议，新增 `DDRG` + `DFTR` 消息 | 「Send via KVM」或把文件拖到主窗口 | 落盘到应用沙箱 `files/Deskflow` |

- hdc 通道：Windows 端拉取默认落到桌面 `from-harmony`；依赖本机 `hdc.exe`
  （DevEco Studio 自带，或设置环境变量 `LINGXI_HDC`，或放到本程序目录）。
- KVM 通道：无需 hdc；单文件上限 200 MB，协议单条上限 1 MB，按 512 KB 分块；
  两端必须都是本合并版，鸿蒙端需已连接且点击「允许使用您的屏幕」。

## 运行

双击 `flux.exe`。`flux-core.exe` / `flux-daemon.exe` 会按需调用。

首次使用请在 **Settings** 页配置：服务端 / 客户端角色、屏幕名、端口。
默认端口 24800，屏幕名 `JARVIS` ↔ `HarmonyPC`。

**File Transfer** 是第二个标签页：左侧本地待发送列表、右侧鸿蒙远程目录、
底部发送/拉取按钮与进度、下方日志。把文件拖到主窗口会**同时**触发 hdc 与 KVM
两条通道；拖到 File Transfer 页内部只加入待发送列表。

## 与已安装版 Deskflow 的关系

Flux 使用独立的 app id `flux`、IPC 名 `flux-core` / `flux-daemon`、共享内存键与
配置目录，因此**不会**与 `C:\Program Files\Deskflow` 的已装版抢占单实例或配置。
但两者都会监听 24800，同时运行会端口冲突 —— 需要共存时请在 Settings 里改端口，
鸿蒙端连接对应端口。

## 目录内容

| 文件 | 说明 |
|---|---|
| `flux.exe` | GUI 主程序（含 File Transfer 标签页） |
| `flux-core.exe` | KVM 核心进程（含 DFTR 发送逻辑） |
| `flux-daemon.exe` | 后台守护进程 |
| `settings\Flux.conf` | 便携配置：走 portable 模式，不读写系统配置目录 |
| `plugins\platforms\qwindows.dll` | Qt Windows 平台插件 |
| `translations\flux_*.qm` | 界面翻译 |
| `docs\` | 上游文档 |
| `README-dist.md` | 本文件 |

不写系统注册表、不覆盖 `C:\Program Files\Deskflow`、删除本目录即卸载。

## 已知限制

- 无安装程序、无开始菜单项、无开机自启。
- hdc 通道依赖 `hdc.exe` 与设备调试授权。
- KVM / DFTR 通道两端必须都是本合并版；无断点续传、无加密、整文件读入内存。
- 单文件 200 MB 上限。

## 重新打包

```
scripts\configure.cmd
scripts\build.cmd
scripts\package.cmd
```

源码与构建脚本见同目录 `deskflow\` 与 `scripts\`。
