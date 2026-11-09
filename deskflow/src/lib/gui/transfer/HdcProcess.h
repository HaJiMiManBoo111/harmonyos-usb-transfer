/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace deskflow::gui::transfer {

struct HdcDevice
{
  QString serial;
  bool isUsb = false;
};

struct HdcRemoteItem
{
  QString name;
  QString fullPath;
  bool isDir = false;
  qint64 size = 0;
};

struct HdcTransferResult
{
  bool success = false;
  QString message;
  int fileCount = 0;
  qint64 totalBytes = 0;
  qint64 elapsedMs = 0;
};

/**
 * @brief 封装对 hdc.exe 的调用：设备枚举、远程目录浏览、文件发送与拉取。
 *
 * 行为对齐 harmony-transfer 的 C# 实现 HdcCore.cs。hdc 的退出码不可靠，
 * 传输成败一律以 stdout 里出现 "FileTransfer finish" 判定。
 *
 * 这些调用是阻塞的（内部用 QProcess 带超时的分段等待，并周期性回调进度）。
 * 需要保持界面响应的上层应在工作线程里调用：本类是 QObject，信号跨线程
 * 走队列连接，进度与日志会投递回接收者所在线程。
 */
class HdcProcess : public QObject
{
  Q_OBJECT

public:
  explicit HdcProcess(QObject *parent = nullptr);
  ~HdcProcess() override;

  /// 定位 hdc.exe。查找顺序：程序同目录 → 环境变量 LINGXI_HDC → PATH → DevEco Studio；
  /// 全部落空时返回程序同目录下的路径，供上层提示「缺少 hdc」。结果会缓存。
  static QString hdcPath();
  static void setHdcPath(const QString &path);

  /// hdc list targets -v。没有设备（或没有 hdc.exe）时返回空列表。
  QList<HdcDevice> listDevices();

  /// 恰好一台设备时返回其 serial；没有设备或多台设备返回空字符串。
  QString resolveDevice();

  /// 与 HdcCore.RemoteRoots 相同的 6 个标准目录。
  QStringList remoteRoots() const;

  /// 列远程目录。目录不存在或没有权限时返回空列表，并通过 logLine 报告原因。
  QList<HdcRemoteItem> listRemote(const QString &remoteDir);

  /// 远程目录的递归文件字节数，失败返回 0。
  qint64 remoteDirSize(const QString &remotePath);

  /// 发送一批本地文件/目录到 remoteDir（空则用鸿蒙桌面）。
  HdcTransferResult send(const QStringList &localPaths, const QString &remoteDir);

  /// 从设备接收一批远程路径到 localDir（空则用 桌面/from-harmony）。
  HdcTransferResult pull(const QStringList &remotePaths, const QString &localDir);

  static QString formatBytes(qint64 bytes);
  static QString formatSpeed(qint64 bytes, qint64 ms);

Q_SIGNALS:
  /// done/total 为字节数；total 为 0 表示进度不确定（目录传输、总量未知）。
  void progress(qint64 done, qint64 total, const QString &currentFile);
  void logLine(const QString &line);

private:
  struct ProcessResult
  {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
  };

  static QString baseDir();
  static QString findOnPath(const QString &fileName);
  static QString findInDevEco();
  static QString sq(const QString &value);
  static QStringList cleanLines(const QString &stdOut);
  static qint64 recursiveSize(const QString &path);
  static QString lastSegment(const QString &path);
  static QString joinRemote(const QString &dir, const QString &name);
  static QString defaultPullDir();
  static QString destLabel(const QString &localDir);
  static QString deviceLabel(const HdcDevice &device);

  /// 取一台设备：优先 USB，其次第一台（与 HdcCore.ResolveDevice 一致）。没有设备返回 false。
  bool pickDevice(HdcDevice *device);

  QString remoteShell(const QString &serial, const QString &command, int timeoutMs);
  qint64 remoteSize(const QString &serial, const QString &remotePath);
  qint64 remoteDirSizeFor(const QString &serial, const QString &remotePath);
  void probeRemoteKind(const QString &serial, const QString &remotePath, bool *isDir, qint64 *size);

  /// hdc 的退出码不可靠：成败一律以 stdout 里是否出现 "FileTransfer finish" 判定。
  static bool hasFinishMarker(const ProcessResult &result);

  /// 跑 hdc 并收集输出。timeoutMs <= 0 表示不设总超时（仍有 8 小时硬上限）。
  /// tickMs > 0 时每 tickMs 调一次 onTick（用于轮询进度）。streamLog 为真时把
  /// stdout 的每一行转发给 logLine。
  ProcessResult runProcess(
      const QStringList &args, int timeoutMs, int tickMs = 0, const std::function<void()> &onTick = {},
      bool streamLog = false
  );

  ProcessResult sendOneFile(
      const QString &serial, const QString &localPath, const QString &remoteTarget, qint64 localSize,
      qint64 sentBefore, qint64 grandTotal, const QString &currentFile
  );
  ProcessResult recvWithProgress(
      const QStringList &args, const QString &watchPath, qint64 itemTotal, qint64 doneBefore, qint64 grandTotal,
      const QString &currentFile
  );

  bool pullOne(
      const QString &serial, const QString &remotePath, const QString &name, const QString &localDir,
      const QString &tmpRoot, qint64 itemTotal, qint64 doneBefore, qint64 grandTotal, QString *error
  );
};

} // namespace deskflow::gui::transfer
