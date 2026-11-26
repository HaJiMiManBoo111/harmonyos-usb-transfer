/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "HdcProcess.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>

namespace Ui {
class FileTransferPage;
}

class QDragEnterEvent;
class QShowEvent;
class QDragMoveEvent;
class QDropEvent;
class QThread;
class QTimer;
class QTreeWidgetItem;

namespace deskflow::gui::transfer {

/**
 * @brief 「文件传输」页：挑选本地文件、浏览鸿蒙设备目录、双向传输。
 *
 * 所有 hdc 调用都是阻塞的，因此 HdcProcess 被 moveToThread 到一个私有工作
 * 线程；页面用 QMetaObject::invokeMethod 派发任务，再把结果投递回 GUI 线程。
 */
class FileTransferPage : public QWidget
{
  Q_OBJECT
public:
  explicit FileTransferPage(QWidget *parent = nullptr);
  ~FileTransferPage() override;

  /// 重新枚举设备，并刷新 hdc 位置提示与远程根目录下拉框。
  void refreshDevices();

  /// 主窗口把拖到程序图标上的路径交给页面：加入待发送列表，设备就绪时立即发送。
  void sendLocalPaths(const QStringList &paths);

  /// 当前 listLocal 中选中的行对应的本地路径（第一列 / UserRole）。
  QStringList selectedLocalPaths() const;

  /// 展示 core 通过 DFTR 链路发来的进度：args = "<已发>/<总> <文件名> <字节>"。
  void showKvmProgress(const QString &args);

  /// 往日志区追加一行（带时间戳），供上层记录 KVM 通道的消息。
  void appendKvmLog(const QString &line);

  /// 请求设备点亮屏幕（PC 从睡眠恢复时调用一次）。设备不在时静默跳过。
  void wakeRemoteScreen();

Q_SIGNALS:
  /// 用户请求通过 KVM（DFTR）连接把给定的本地文件发给远端。
  void sendViaKvmRequested(const QStringList &paths);

protected:
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private Q_SLOTS:
  /// 定时重扫已经在线的鸿蒙设备（开机自检、设备晚到、hdc server 重启后自动恢复）。
  void pollDevices();
  void onBrowseFiles();
  void onBrowseFolder();
  void onRefreshRemote();
  void onRemoteItemActivated(QTreeWidgetItem *item, int column);
  void onGoUpRemote();
  void onSend();
  void onPull();
  void onSendViaKvm();
  void onProgress(qint64 done, qint64 total, const QString &currentFile);
  void onLogLine(const QString &line);

private:
  void buildUi();
  void setupConnections();
  void startWorker();
  void stopWorker();
  void appendLog(const QString &line);
  void setBusy(bool busy);
  void updateDeviceLabel();
  /// 按 lblHdcPath 当前可用宽度重新省略 hdc 路径（窗口缩放时调用）。
  void updateHdcElide();
  /// 远程路径按标签当前宽度做中间省略，避免右侧被硬切掉。
  void updateRemotePathElide();
  /// 首次拿到真实高度后按 3:2 / 1:1 切一次分隔条；之后交给用户拖动。
  void seedSplitterSizes();

  // 本地待发送列表
  void addLocalPaths(const QStringList &paths);
  bool addLocalPath(const QString &path);
  bool hasLocalPath(const QString &path) const;
  QStringList localPaths() const;
  void removeSelectedLocalPaths();
  void clearLocalPaths();

  // 远程浏览
  void navigateRemote(const QString &dir);
  void requestRemoteListing(const QString &dir);
  void applyRemoteListing(const QString &dir, const QList<HdcRemoteItem> &items);
  QStringList selectedRemotePaths() const;

  // 工作线程派发与结果回灌
  void requestDeviceRefresh();
  void applyDevices(const QList<HdcDevice> &devices, const QString &resolved);
  void applyRemoteRoots(const QStringList &roots);
  /// 远程根目录的短显示名（桌面 / 下载 / 文档 …），完整路径仍存在 UserRole 里。
  static QString shortRootName(const QString &path);
  void applyTransferResult(const HdcTransferResult &result, bool upload);

  void updateProgressLabel(qint64 done, qint64 total, const QString &currentFile);
  void openPullDirectory();
  static QString defaultPullDirectory();

  std::unique_ptr<Ui::FileTransferPage> ui;
  QThread *m_workerThread = nullptr;
  HdcProcess *m_hdc = nullptr;
  /// 设备重扫定时器：仅在未发现设备时运行，发现后自动停止。
  QTimer *m_devicePollTimer = nullptr;

  QList<HdcDevice> m_devices;
  QString m_deviceSerial;
  QString m_remotePath = QStringLiteral("/");
  QString m_pullDir;
  bool m_busy = false;
  bool m_listing = false;
  qint64 m_transferStartMs = 0;
  /// 未省略的 "hdc: <路径>" 文本；实际显示时按当前宽度做 ElideMiddle。
  QString m_hdcLabel;
  QString m_remotePathLabel;
  /// resize 后延迟一次重算省略，等布局真正分配完宽度。
  bool m_relayoutPending = false;
  /// 主分隔条是否已按真实高度切过一次 3:2。
  /// 用户手动拖过分隔条后，不再自动等分。
  bool m_userSplit = false;
  /// 「未发现设备」日志只打一次，避免定时重扫每 5 秒刷一行。
  bool m_noDeviceLogged = false;
};

} // namespace deskflow::gui::transfer
