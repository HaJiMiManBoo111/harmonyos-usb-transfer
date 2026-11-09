/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferPage.h"
#include "ui_FileTransferPage.h"

#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>

namespace {

constexpr int kShutdownWaitMs = 5000;
constexpr int kProgressScale = 1000;
constexpr auto kErrorStyle = "color: crimson;";

QString timestamp()
{
  return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

/// 远程路径的父目录；已经在 / 时返回自身。
QString parentRemotePath(const QString &path)
{
  QString trimmed = path;
  while (trimmed.size() > 1 && trimmed.endsWith(QLatin1Char('/'))) {
    trimmed.chop(1);
  }
  const auto slash = trimmed.lastIndexOf(QLatin1Char('/'));
  if (slash <= 0) {
    return QStringLiteral("/");
  }
  return trimmed.left(static_cast<int>(slash));
}

} // namespace

namespace deskflow::gui::transfer {

FileTransferPage::FileTransferPage(QWidget *parent)
    : QWidget{parent},
      ui{std::make_unique<Ui::FileTransferPage>()}
{
  ui->setupUi(this);

  buildUi();
  setupConnections();
  startWorker();
  refreshDevices();
}

FileTransferPage::~FileTransferPage()
{
  stopWorker();
}

void FileTransferPage::buildUi()
{
  setAcceptDrops(true);

  QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  logFont.setStyleHint(QFont::Monospace);
  ui->textLog->setFont(logFont);
  ui->textLog->setMaximumBlockCount(2000);
  ui->textLog->setReadOnly(true);
  ui->textLog->setLineWrapMode(QPlainTextEdit::NoWrap);

  ui->listLocal->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  ui->listLocal->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

  ui->treeRemote->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  ui->treeRemote->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  ui->treeRemote->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

  ui->progressBar->setRange(0, kProgressScale);
  ui->progressBar->setValue(0);
  ui->progressBar->setFormat(tr("%p%"));

  ui->lblRemotePath->setText(m_remotePath);
  ui->btnRemoteUp->setEnabled(false);

  ui->btnSend->setEnabled(false);
  ui->btnPull->setEnabled(false);
  ui->btnSendViaKvm->setEnabled(false);
}

void FileTransferPage::setupConnections()
{
  connect(ui->btnRefreshDevice, &QPushButton::clicked, this, &FileTransferPage::refreshDevices);
  connect(ui->btnAddFiles, &QPushButton::clicked, this, &FileTransferPage::onBrowseFiles);
  connect(ui->btnAddFolder, &QPushButton::clicked, this, &FileTransferPage::onBrowseFolder);
  connect(ui->btnRemoveSelected, &QPushButton::clicked, this, &FileTransferPage::removeSelectedLocalPaths);
  connect(ui->btnClearList, &QPushButton::clicked, this, &FileTransferPage::clearLocalPaths);
  connect(ui->btnRemoteUp, &QPushButton::clicked, this, &FileTransferPage::onGoUpRemote);
  connect(ui->btnRemoteRefresh, &QPushButton::clicked, this, &FileTransferPage::onRefreshRemote);
  connect(ui->btnSend, &QPushButton::clicked, this, &FileTransferPage::onSend);
  connect(ui->btnPull, &QPushButton::clicked, this, &FileTransferPage::onPull);
  connect(ui->btnOpenPullDir, &QPushButton::clicked, this, &FileTransferPage::openPullDirectory);
  connect(ui->btnSendViaKvm, &QPushButton::clicked, this, &FileTransferPage::onSendViaKvm);
  connect(
      ui->treeRemote, &QTreeWidget::itemActivated, this, &FileTransferPage::onRemoteItemActivated
  );
  connect(ui->treeRemote, &QTreeWidget::itemDoubleClicked, this, &FileTransferPage::onRemoteItemActivated);
  connect(
      ui->cbRemoteRoot, qOverload<int>(&QComboBox::activated), this,
      [this](int index) {
        Q_UNUSED(index)
        navigateRemote(ui->cbRemoteRoot->currentText());
      }
  );
}

void FileTransferPage::startWorker()
{
  if (m_workerThread != nullptr) {
    return;
  }

  m_workerThread = new QThread(this);
  m_workerThread->setObjectName(QStringLiteral("FileTransferHdc"));

  m_hdc = new HdcProcess;
  m_hdc->moveToThread(m_workerThread);

  connect(m_workerThread, &QThread::finished, m_hdc, &QObject::deleteLater);
  connect(m_hdc, &HdcProcess::progress, this, &FileTransferPage::onProgress);
  connect(m_hdc, &HdcProcess::logLine, this, &FileTransferPage::onLogLine);

  m_workerThread->start();
}

void FileTransferPage::stopWorker()
{
  if (m_workerThread == nullptr) {
    return;
  }

  m_workerThread->requestInterruption();
  m_workerThread->quit();
  if (!m_workerThread->wait(kShutdownWaitMs)) {
    // A transfer is still running a blocking hdc call; nothing can cancel it
    // gracefully, so the thread has to be forced down while the window closes.
    m_workerThread->terminate();
    m_workerThread->wait();
  }
  m_workerThread = nullptr;
  m_hdc = nullptr;
}

void FileTransferPage::refreshDevices()
{
  appendLog(tr("正在检测鸿蒙设备…"));
  ui->lblDevice->setText(tr("正在检测设备…"));
  ui->lblDevice->setStyleSheet(QString());
  requestDeviceRefresh();
}

void FileTransferPage::requestDeviceRefresh()
{
  if (m_hdc == nullptr) {
    return;
  }

  QMetaObject::invokeMethod(
      m_hdc,
      [this] {
        const QList<HdcDevice> devices = m_hdc->listDevices();
        const QString resolved = m_hdc->resolveDevice();
        QMetaObject::invokeMethod(
            this, [this, devices, resolved] { applyDevices(devices, resolved); }, Qt::QueuedConnection
        );
      },
      Qt::QueuedConnection
  );
}

void FileTransferPage::applyDevices(const QList<HdcDevice> &devices, const QString &resolved)
{
  m_devices = devices;

  if (!resolved.isEmpty()) {
    m_deviceSerial = resolved;
  } else if (m_deviceSerial.isEmpty() && !devices.isEmpty()) {
    m_deviceSerial = devices.first().serial;
  } else if (devices.isEmpty()) {
    m_deviceSerial.clear();
  }

  updateDeviceLabel();

  if (devices.isEmpty()) {
    appendLog(tr("未发现设备。请连接 USB 并在设备上允许调试。"));
    ui->treeRemote->clear();
    ui->btnSend->setEnabled(false);
    ui->btnPull->setEnabled(false);
    return;
  }

  applyRemoteRoots(m_hdc->remoteRoots());

  appendLog(tr("设备已就绪：%1").arg(m_deviceSerial));
  ui->btnSend->setEnabled(!m_busy && ui->listLocal->topLevelItemCount() > 0);
  ui->btnPull->setEnabled(!m_busy);

  navigateRemote(m_remotePath);
}

void FileTransferPage::updateDeviceLabel()
{
  const QString hdc = HdcProcess::hdcPath();
  const bool hdcMissing = hdc.isEmpty() || !QFileInfo::exists(hdc);
  ui->lblHdcPath->setText(hdcMissing ? tr("hdc: 未找到") : tr("hdc: %1").arg(hdc));
  ui->lblHdcPath->setToolTip(hdc);
  ui->lblHdcPath->setStyleSheet(hdcMissing ? QString::fromLatin1(kErrorStyle) : QString());

  if (m_devices.isEmpty()) {
    ui->lblDevice->setText(tr("未检测到鸿蒙设备（请连接 USB 并开启调试）"));
    ui->lblDevice->setStyleSheet(QString::fromLatin1(kErrorStyle));
    return;
  }

  QString text;
  if (m_devices.size() == 1) {
    const HdcDevice &device = m_devices.first();
    text = tr("已连接：%1（%2）")
               .arg(m_deviceSerial, device.isUsb ? tr("USB") : tr("网络"));
  } else {
    text = tr("已连接 %1 台设备，当前使用：%2").arg(QString::number(m_devices.size()), m_deviceSerial);
  }
  ui->lblDevice->setText(text);
  ui->lblDevice->setStyleSheet(QString());
}

void FileTransferPage::applyRemoteRoots(const QStringList &roots)
{
  const QSignalBlocker blocker(ui->cbRemoteRoot);
  ui->cbRemoteRoot->clear();
  for (const QString &root : roots) {
    ui->cbRemoteRoot->addItem(root);
  }
}

void FileTransferPage::onBrowseFiles()
{
  const QStringList files =
      QFileDialog::getOpenFileNames(this, tr("选择要发送的文件"), QDir::homePath());
  if (!files.isEmpty()) {
    addLocalPaths(files);
  }
}

void FileTransferPage::onBrowseFolder()
{
  const QString dir = QFileDialog::getExistingDirectory(this, tr("选择要发送的文件夹"), QDir::homePath());
  if (!dir.isEmpty()) {
    addLocalPaths({dir});
  }
}

void FileTransferPage::addLocalPaths(const QStringList &paths)
{
  int added = 0;
  for (const QString &path : paths) {
    if (addLocalPath(path)) {
      ++added;
    }
  }

  if (added > 0) {
    appendLog(tr("已加入 %1 个路径，共 %2 项待发送。")
                  .arg(QString::number(added), QString::number(ui->listLocal->topLevelItemCount())));
  }

  ui->btnSend->setEnabled(!m_busy && !m_devices.isEmpty() && ui->listLocal->topLevelItemCount() > 0);
  ui->btnSendViaKvm->setEnabled(ui->listLocal->topLevelItemCount() > 0);
}

bool FileTransferPage::addLocalPath(const QString &path)
{
  const QFileInfo info(path);
  if (!info.exists()) {
    appendLog(tr("跳过不存在的路径：%1").arg(path));
    return false;
  }

  const QString absolute = info.absoluteFilePath();
  if (hasLocalPath(absolute)) {
    return false;
  }

  auto *item = new QTreeWidgetItem(ui->listLocal);
  item->setText(0, absolute);
  item->setText(1, info.isDir() ? tr("目录") : HdcProcess::formatBytes(info.size()));
  item->setToolTip(0, absolute);
  item->setData(0, Qt::UserRole, absolute);
  return true;
}

bool FileTransferPage::hasLocalPath(const QString &path) const
{
  for (int i = 0; i < ui->listLocal->topLevelItemCount(); ++i) {
    if (ui->listLocal->topLevelItem(i)->data(0, Qt::UserRole).toString() == path) {
      return true;
    }
  }
  return false;
}

QStringList FileTransferPage::localPaths() const
{
  QStringList paths;
  paths.reserve(ui->listLocal->topLevelItemCount());
  for (int i = 0; i < ui->listLocal->topLevelItemCount(); ++i) {
    paths.append(ui->listLocal->topLevelItem(i)->data(0, Qt::UserRole).toString());
  }
  return paths;
}

QStringList FileTransferPage::selectedLocalPaths() const
{
  QStringList paths;
  const QList<QTreeWidgetItem *> selected = ui->listLocal->selectedItems();
  for (const QTreeWidgetItem *item : selected) {
    const QString path = item->data(0, Qt::UserRole).toString();
    if (!path.isEmpty()) {
      paths.append(path);
    }
  }
  return paths;
}

void FileTransferPage::removeSelectedLocalPaths()
{
  const QList<QTreeWidgetItem *> selected = ui->listLocal->selectedItems();
  for (QTreeWidgetItem *item : selected) {
    delete item;
  }
  ui->btnSend->setEnabled(!m_busy && !m_devices.isEmpty() && ui->listLocal->topLevelItemCount() > 0);
  ui->btnSendViaKvm->setEnabled(ui->listLocal->topLevelItemCount() > 0);
}

void FileTransferPage::clearLocalPaths()
{
  ui->listLocal->clear();
  ui->btnSend->setEnabled(false);
  ui->btnSendViaKvm->setEnabled(false);
}

void FileTransferPage::onRefreshRemote()
{
  requestRemoteListing(m_remotePath);
}

void FileTransferPage::onGoUpRemote()
{
  const QString parent = parentRemotePath(m_remotePath);
  if (parent == m_remotePath) {
    return;
  }
  navigateRemote(parent);
}

void FileTransferPage::navigateRemote(const QString &dir)
{
  const QString target = dir.trimmed().isEmpty() ? QStringLiteral("/") : dir;
  m_remotePath = target;
  ui->lblRemotePath->setText(target);
  ui->btnRemoteUp->setEnabled(target != QStringLiteral("/"));
  requestRemoteListing(target);
}

void FileTransferPage::requestRemoteListing(const QString &dir)
{
  if (m_hdc == nullptr) {
    return;
  }

  ui->treeRemote->setEnabled(false);
  ui->treeRemote->clear();
  appendLog(tr("读取远程目录：%1").arg(dir));
  m_listing = true;

  QMetaObject::invokeMethod(
      m_hdc,
      [this, dir] {
        const QList<HdcRemoteItem> items = m_hdc->listRemote(dir);
        QMetaObject::invokeMethod(
            this, [this, dir, items] { applyRemoteListing(dir, items); }, Qt::QueuedConnection
        );
      },
      Qt::QueuedConnection
  );
}

void FileTransferPage::applyRemoteListing(const QString &dir, const QList<HdcRemoteItem> &items)
{
  m_listing = false;
  ui->treeRemote->setEnabled(true);
  ui->treeRemote->clear();

  if (m_remotePath != dir) {
    return;
  }

  for (const HdcRemoteItem &entry : items) {
    auto *item = new QTreeWidgetItem(ui->treeRemote);
    item->setText(0, entry.name);
    item->setText(1, entry.isDir ? QStringLiteral("—") : HdcProcess::formatBytes(entry.size));
    item->setText(2, entry.isDir ? tr("目录") : tr("文件"));
    item->setData(0, Qt::UserRole, entry.fullPath);
    item->setData(0, Qt::UserRole + 1, entry.isDir);
    item->setToolTip(0, entry.fullPath);
  }

  appendLog(tr("共 %1 项。").arg(QString::number(items.size())));
}

QStringList FileTransferPage::selectedRemotePaths() const
{
  QStringList paths;
  const QList<QTreeWidgetItem *> selected = ui->treeRemote->selectedItems();
  for (const QTreeWidgetItem *item : selected) {
    const QString path = item->data(0, Qt::UserRole).toString();
    if (!path.isEmpty()) {
      paths.append(path);
    }
  }
  return paths;
}

void FileTransferPage::onRemoteItemActivated(QTreeWidgetItem *item, int column)
{
  Q_UNUSED(column)
  if (item == nullptr || !item->data(0, Qt::UserRole + 1).toBool()) {
    return;
  }

  const QString path = item->data(0, Qt::UserRole).toString();
  if (!path.isEmpty()) {
    navigateRemote(path);
  }
}

void FileTransferPage::onSend()
{
  if (m_busy) {
    return;
  }

  const QStringList paths = localPaths();
  if (paths.isEmpty()) {
    appendLog(tr("没有待发送的本地文件。"));
    return;
  }

  if (m_devices.isEmpty()) {
    appendLog(tr("没有可用设备，请先连接鸿蒙设备并刷新。"));
    refreshDevices();
    return;
  }

  const QString remoteDir = m_remotePath;
  appendLog(tr("开始发送 %1 项到 %2 …").arg(QString::number(paths.size()), remoteDir));

  m_transferStartMs = QDateTime::currentMSecsSinceEpoch();
  setBusy(true);

  QMetaObject::invokeMethod(
      m_hdc,
      [this, paths, remoteDir] {
        const HdcTransferResult result = m_hdc->send(paths, remoteDir);
        QMetaObject::invokeMethod(
            this, [this, result] { applyTransferResult(result, true); }, Qt::QueuedConnection
        );
      },
      Qt::QueuedConnection
  );
}

void FileTransferPage::onPull()
{
  if (m_busy) {
    return;
  }

  const QStringList paths = selectedRemotePaths();
  if (paths.isEmpty()) {
    appendLog(tr("请先在右侧选中要拉取的文件或目录。"));
    return;
  }

  if (m_devices.isEmpty()) {
    appendLog(tr("没有可用设备，请先连接鸿蒙设备并刷新。"));
    refreshDevices();
    return;
  }

  if (m_pullDir.isEmpty()) {
    m_pullDir = defaultPullDirectory();
  }
  const QString localDir = m_pullDir;
  QDir().mkpath(localDir);

  appendLog(tr("开始从鸿蒙拉取 %1 项到 %2 …").arg(QString::number(paths.size()), localDir));

  m_transferStartMs = QDateTime::currentMSecsSinceEpoch();
  setBusy(true);

  QMetaObject::invokeMethod(
      m_hdc,
      [this, paths, localDir] {
        const HdcTransferResult result = m_hdc->pull(paths, localDir);
        QMetaObject::invokeMethod(
            this, [this, result] { applyTransferResult(result, false); }, Qt::QueuedConnection
        );
      },
      Qt::QueuedConnection
  );
}

void FileTransferPage::onSendViaKvm()
{
  QStringList paths = selectedLocalPaths();
  if (paths.isEmpty()) {
    paths = localPaths();
  }

  if (paths.isEmpty()) {
    appendLog(QStringLiteral("no file selected"));
    return;
  }

  appendLog(
      QStringLiteral("sending %1 file(s) over the KVM link").arg(QString::number(paths.size()))
  );
  Q_EMIT sendViaKvmRequested(paths);
}

void FileTransferPage::applyTransferResult(const HdcTransferResult &result, bool upload)
{
  setBusy(false);

  const QString elapsed = QString::number(result.elapsedMs);
  if (result.success) {
    ui->progressBar->setValue(kProgressScale);
    const QString summary = upload
                                ? tr("发送完成：%1 个文件，%2，耗时 %3 毫秒")
                                      .arg(QString::number(result.fileCount),
                                           HdcProcess::formatBytes(result.totalBytes), elapsed)
                                : tr("拉取完成：%1 个文件，%2，耗时 %3 毫秒")
                                      .arg(QString::number(result.fileCount),
                                           HdcProcess::formatBytes(result.totalBytes), elapsed);
    appendLog(summary);
    ui->lblProgress->setText(summary);
    ui->lblProgress->setStyleSheet(QString());

    if (upload) {
      requestRemoteListing(m_remotePath);
    }
    return;
  }

  ui->progressBar->setValue(0);
  ui->lblProgress->setText(tr("失败：%1").arg(result.message));
  ui->lblProgress->setStyleSheet(QString::fromLatin1(kErrorStyle));
  appendLog(tr("传输失败：%1").arg(result.message));
}

void FileTransferPage::onProgress(qint64 done, qint64 total, const QString &currentFile)
{
  if (m_transferStartMs <= 0) {
    m_transferStartMs = QDateTime::currentMSecsSinceEpoch();
  }

  if (total > 0) {
    ui->progressBar->setRange(0, kProgressScale);
    const qint64 scaled = done * kProgressScale / total;
    ui->progressBar->setValue(static_cast<int>(qBound<qint64>(qint64{0}, scaled, qint64{kProgressScale})));
  } else {
    ui->progressBar->setRange(0, 0);
  }

  updateProgressLabel(done, total, currentFile);
}

void FileTransferPage::updateProgressLabel(qint64 done, qint64 total, const QString &currentFile)
{
  const qint64 elapsedMs =
      qMax<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_transferStartMs);
  const QString speed = HdcProcess::formatSpeed(done, elapsedMs);

  QString text;
  if (total > 0) {
    text = tr("%1 / %2 · %3")
               .arg(HdcProcess::formatBytes(done), HdcProcess::formatBytes(total), speed);
    if (done > 0 && total > done) {
      const qint64 remainingMs = elapsedMs * (total - done) / done;
      text += tr(" · 剩余 %1 秒").arg(QString::number(remainingMs / 1000.0, 'f', 1));
    }
  } else {
    text = tr("%1 · %2").arg(HdcProcess::formatBytes(done), speed);
  }

  if (!currentFile.isEmpty()) {
    text += QStringLiteral(" · ") + currentFile;
  }
  ui->lblProgress->setText(text);
}

void FileTransferPage::onLogLine(const QString &line)
{
  appendLog(line);
}

void FileTransferPage::appendLog(const QString &line)
{
  ui->textLog->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp(), line));
}

void FileTransferPage::appendKvmLog(const QString &line)
{
  appendLog(line);
}

void FileTransferPage::showKvmProgress(const QString &args)
{
  if (args.isEmpty()) {
    return;
  }

  appendLog(args);

  // args format: "<sent>/<total> <file name> <bytes>"
  const auto parts = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (parts.isEmpty()) {
    return;
  }

  const auto counts = parts.at(0).split(QLatin1Char('/'));
  int sent = 0;
  int total = 0;
  if (counts.size() >= 2) {
    sent = counts.at(0).toInt();
    total = counts.at(1).toInt();
  }

  if (total > 0) {
    ui->progressBar->setRange(0, kProgressScale);
    const qint64 scaled = static_cast<qint64>(sent) * kProgressScale / total;
    ui->progressBar->setValue(static_cast<int>(qBound<qint64>(qint64{0}, scaled, qint64{kProgressScale})));
  }

  ui->lblProgress->setText(QStringLiteral("KVM %1").arg(args));
}

void FileTransferPage::setBusy(bool busy)
{
  m_busy = busy;

  ui->btnRefreshDevice->setEnabled(!busy);
  ui->btnAddFiles->setEnabled(!busy);
  ui->btnAddFolder->setEnabled(!busy);
  ui->btnRemoveSelected->setEnabled(!busy);
  ui->btnClearList->setEnabled(!busy);
  ui->btnRemoteRefresh->setEnabled(!busy);
  ui->btnRemoteUp->setEnabled(!busy && m_remotePath != QStringLiteral("/"));
  ui->btnSend->setEnabled(!busy && !m_devices.isEmpty() && ui->listLocal->topLevelItemCount() > 0);
  ui->btnPull->setEnabled(!busy && !m_devices.isEmpty());
  ui->btnSendViaKvm->setEnabled(ui->listLocal->topLevelItemCount() > 0);

  if (busy) {
    ui->progressBar->setRange(0, 0);
    ui->lblProgress->setText(tr("传输中…"));
    ui->lblProgress->setStyleSheet(QString());
  } else {
    ui->progressBar->setRange(0, kProgressScale);
  }
}

void FileTransferPage::openPullDirectory()
{
  QString dir = m_pullDir;
  if (dir.isEmpty()) {
    dir = defaultPullDirectory();
    m_pullDir = dir;
  }

  if (!QDir().mkpath(dir)) {
    appendLog(tr("无法创建接收目录：%1").arg(dir));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QString FileTransferPage::defaultPullDirectory()
{
  const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
  const QString base = desktop.isEmpty() ? QDir::homePath() : desktop;
  return QDir(base).filePath(QStringLiteral("from-harmony"));
}

void FileTransferPage::sendLocalPaths(const QStringList &paths)
{
  if (paths.isEmpty()) {
    return;
  }

  addLocalPaths(paths);
  if (m_busy) {
    appendLog(tr("当前有传输正在进行，稍后再发送。"));
    return;
  }

  if (m_devices.isEmpty()) {
    appendLog(tr("已加入待发送列表；设备就绪后点击「发送到鸿蒙」。"));
    refreshDevices();
    return;
  }

  onSend();
}

void FileTransferPage::dragEnterEvent(QDragEnterEvent *event)
{
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
    return;
  }
  QWidget::dragEnterEvent(event);
}

void FileTransferPage::dragMoveEvent(QDragMoveEvent *event)
{
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
    return;
  }
  QWidget::dragMoveEvent(event);
}

void FileTransferPage::dropEvent(QDropEvent *event)
{
  if (!event->mimeData()->hasUrls()) {
    QWidget::dropEvent(event);
    return;
  }

  QStringList paths;
  const QList<QUrl> urls = event->mimeData()->urls();
  for (const QUrl &url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }
    const QString path = url.toLocalFile();
    if (!path.isEmpty()) {
      paths.append(path);
    }
  }

  event->acceptProposedAction();
  if (!paths.isEmpty()) {
    addLocalPaths(paths);
  }
}

} // namespace deskflow::gui::transfer
