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
class QDragMoveEvent;
class QDropEvent;
class QThread;
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

Q_SIGNALS:
  /// 用户请求通过 KVM（DFTR）连接把给定的本地文件发给远端。
  void sendViaKvmRequested(const QStringList &paths);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private Q_SLOTS:
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
  void applyTransferResult(const HdcTransferResult &result, bool upload);

  void updateProgressLabel(qint64 done, qint64 total, const QString &currentFile);
  void openPullDirectory();
  static QString defaultPullDirectory();

  std::unique_ptr<Ui::FileTransferPage> ui;
  QThread *m_workerThread = nullptr;
  HdcProcess *m_hdc = nullptr;

  QList<HdcDevice> m_devices;
  QString m_deviceSerial;
  QString m_remotePath = QStringLiteral("/");
  QString m_pullDir;
  bool m_busy = false;
  bool m_listing = false;
  qint64 m_transferStartMs = 0;
};

} // namespace deskflow::gui::transfer
