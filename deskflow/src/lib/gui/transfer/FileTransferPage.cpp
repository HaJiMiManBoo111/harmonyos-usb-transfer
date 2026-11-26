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
#include <QFontMetrics>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>

namespace {

constexpr int kShutdownWaitMs = 5000;
constexpr int kProgressScale = 1000;
/// 未发现设备时的重扫间隔。开机后 hdc server / USB 枚举往往晚于 Flux 启动，
/// 构造时那一次 listDevices() 会拿到空列表；这里定时重扫直到设备出现。
constexpr int kDevicePollIntervalMs = 5000;
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

  // 开机后 hdc server / USB 枚举往往晚于 Flux 启动，构造时那一次检测可能
  // 拿到空列表；这里定时重扫，直到发现设备后自行停止（见 pollDevices）。
  m_devicePollTimer = new QTimer(this);
  m_devicePollTimer->setObjectName(QStringLiteral("DevicePolling"));
  m_devicePollTimer->setInterval(kDevicePollIntervalMs);
  connect(m_devicePollTimer, &QTimer::timeout, this, &FileTransferPage::pollDevices);
  m_devicePollTimer->start();

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

  // Flux: 三层嵌套分隔条 —— 左右拖动分两个文件列表，上下拖动分「文件区 / 日志区」。
  // 旧实现把尺寸硬编码成 {900, 110}：窗口再高日志区也只有 110 单位，
  // 剩余空间全给文件列表，日志卡片被挤出视口、底部留一片 128px 死白。
  // 现在上半区 3 : 下半区 2，且下限各自约束住，实际像素在 resizeEvent()
  // 拿到真实高度后按比例切分（见 seedSplitterSizes()）。
  ui->splitter->setMinimumHeight(120);
  ui->listPaneLayout->setStretch(0, 1);
  ui->listPaneLayout->setStretch(1, 0);
  ui->listPaneLayout->setStretch(2, 0);
  // 日志卡片下限只给 120：窗口缩到 760 高时 200 会独吞掉一半，
  // 文件列表被压得只剩表头（实测 Tree 高 48px）。
  ui->groupLog->setMinimumHeight(120);
  ui->mainSplitter->setStretchFactor(0, 3);
  ui->mainSplitter->setStretchFactor(1, 2);
  connect(ui->splitter, &QSplitter::splitterMoved, this, [this] { m_userSplit = true; });
  ui->splitter->setStretchFactor(0, 1);
  ui->splitter->setStretchFactor(1, 1);
  ui->pageLayout->setStretch(2, 1);

  ui->progressBar->setRange(0, kProgressScale);
  ui->progressBar->setValue(0);
  ui->progressBar->setFormat(tr("%p%"));

  m_remotePathLabel = m_remotePath;
  updateRemotePathElide();
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
        navigateRemote(ui->cbRemoteRoot->currentData().toString());
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

void FileTransferPage::pollDevices()
{
  // 定时静默重扫：不写「正在检测」日志，也不改 lblDevice 文案，避免每 5 秒
  // 刷一次界面。真正发现设备后由 applyDevices() 停止定时器。
  requestDeviceRefresh();
}

void FileTransferPage::refreshDevices()
{
  // 手动刷新 / 发送时重扫：若设备曾经连上又断开（定时器已被停掉），
  // 这里把轮询重新拉起，直到设备再次出现。
  if (m_devicePollTimer != nullptr && !m_devicePollTimer->isActive()) {
    m_devicePollTimer->start();
  }
  appendLog(tr("正在检测鸿蒙设备…"));
  ui->lblDevice->setText(tr("正在检测设备…"));
  ui->lblDevice->setStyleSheet(QString());
  requestDeviceRefresh();
}

void FileTransferPage::wakeRemoteScreen()
{
  if (m_deviceSerial.isEmpty() || m_hdc == nullptr) {
    return;
  }

  const QString serial = m_deviceSerial;
  QMetaObject::invokeMethod(
      m_hdc,
      [this, serial] {
        const QString output = m_hdc->wakeScreen(serial);
        QMetaObject::invokeMethod(
            this,
            [this, output] {
              if (output.isEmpty()) {
                appendLog(tr("已请求设备点亮屏幕"));
              } else {
                appendLog(tr("已请求设备点亮屏幕：%1").arg(output));
              }
            },
            Qt::QueuedConnection
        );
      },
      Qt::QueuedConnection
  );
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
    if (!m_noDeviceLogged) {
      appendLog(tr("未发现设备。请连接 USB 并在设备上允许调试；将每 5 秒自动重扫。"));
      m_noDeviceLogged = true;
    }
    ui->treeRemote->clear();
    ui->btnSend->setEnabled(false);
    ui->btnPull->setEnabled(false);
    return;
  }

  // 设备已出现：停止轮询，避免后续无谓的 hdc 子进程；状态位移复位，
  // 便于设备离线后被 manual refresh 再次拉起轮询时能重新记录。
  if (m_devicePollTimer != nullptr && m_devicePollTimer->isActive()) {
    m_devicePollTimer->stop();
  }
  m_noDeviceLogged = false;

  applyRemoteRoots(m_hdc->remoteRoots());

  // '/' 在鸿蒙侧不可读（uid=2000(shell) stat '/' 被拒），首次进入/刷新设备时
  // 直接落到第一个可读的标准目录，远程列表才不会一片空白。
  if (m_remotePath == QStringLiteral("/") && ui->cbRemoteRoot->count() > 0) {
    m_remotePath = ui->cbRemoteRoot->itemData(0).toString();
  }

  appendLog(tr("设备已就绪：%1").arg(m_deviceSerial));
  ui->btnSend->setEnabled(!m_busy && ui->listLocal->topLevelItemCount() > 0);
  ui->btnPull->setEnabled(!m_busy);

  navigateRemote(m_remotePath);
}

void FileTransferPage::updateDeviceLabel()
{
  const QString hdc = HdcProcess::hdcPath();
  const bool hdcMissing = hdc.isEmpty() || !QFileInfo::exists(hdc);
  m_hdcLabel = hdcMissing ? tr("hdc: 未找到") : tr("hdc: %1").arg(hdc);
  updateHdcElide();
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

void FileTransferPage::updateHdcElide()
{
  if (m_hdcLabel.isEmpty()) {
    ui->lblHdcPath->setText(QString());
    return;
  }
  // Flux: 省略宽度改为跟随控件实际可用宽度。旧实现硬编码 240 px，
  // 窗口再宽也只显示 "C:/Program File.../toolchains/hdc.exe"。
  const int avail = qMax(120, ui->lblHdcPath->width() - 4);
  const QFontMetrics fm(ui->lblHdcPath->fontMetrics());
  ui->lblHdcPath->setText(fm.elidedText(m_hdcLabel, Qt::ElideMiddle, avail));
}

void FileTransferPage::updateRemotePathElide()
{
  if (m_remotePathLabel.isEmpty()) {
    ui->lblRemotePath->setText(QString());
    return;
  }

  // 下拉框已经显示了当前根目录的短名（桌面 / 下载 / …），这里再重复一遍
  // 整串 /storage/media/100/local/files/Docs/Desktop 就是同一路径写两遍。
  // 所以标签只显示「相对当前根目录」的部分：根目录本身显示 "/"，更深才显
  // 示 "/子目录/…"，完整路径留在 tooltip 里。
  QString shown = m_remotePathLabel;
  QString bestRoot;
  for (int i = 0; i < ui->cbRemoteRoot->count(); ++i) {
    const QString root = ui->cbRemoteRoot->itemData(i).toString();
    if (root.isEmpty() || !m_remotePathLabel.startsWith(root)) {
      continue;
    }
    if (root.length() > bestRoot.length()) {
      bestRoot = root;
    }
  }
  if (!bestRoot.isEmpty()) {
    shown = m_remotePathLabel.mid(bestRoot.length());
    if (shown.isEmpty()) {
      shown = QStringLiteral("/");
    }
  }

  const int avail = qMax(80, ui->lblRemotePath->width() - 8);
  const QFontMetrics fm(ui->lblRemotePath->fontMetrics());
  ui->lblRemotePath->setText(fm.elidedText(shown, Qt::ElideMiddle, avail));
}

void FileTransferPage::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  seedSplitterSizes();
  // 宽度要等父布局分配完才准；先立即算一次，再排一次延迟重算。
  updateHdcElide();
  updateRemotePathElide();
  if (!m_relayoutPending) {
    m_relayoutPending = true;
    QTimer::singleShot(0, this, [this] {
      m_relayoutPending = false;
      updateHdcElide();
      updateRemotePathElide();
    });
  }

}

void FileTransferPage::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);

  // Flux: 窗口第一次真正显示出来之后才知道分隔条的真实高度，所以切分放在
  // showEvent 里的一次零延时回调里做，而不是 buildUi()（那时 height() 还是 0
  // 或布局前的旧值）。旧实现在 buildUi() 硬编码 {900,110}，日志区永远只有
  // ~110 单位，剩下全给文件列表，日志卡片被挤出视口、底部留一片死白。
  QTimer::singleShot(0, this, [this] { seedSplitterSizes(); });
}

void FileTransferPage::seedSplitterSizes()
{
  // 每次窗口尺寸变化都按 3:2 重算一次（用户手动拖过之后就不再干预）。
  // 之前用「只切一次」的标记有坑：窗口从 1176 高缩到 760 高时，
  // QSplitter 保留上一次的绝对像素，上半区被压到只剩标题栏，日志区反而
  // 独吞了大半 —— 越小越难看。改成每次 resize 按比例重算最稳。
  if (m_userSplit) {
    return;
  }
  const int total = ui->mainSplitter->height() - ui->mainSplitter->handleWidth();
  if (total > 260) {
    // 下半区（日志）取 2/5，但夹在 140 与 total-220 之间：
    // 太矮日志看不清，太高文件列表就只剩表头。
    int bottom = total * 2 / 5;
    bottom = qBound(170, bottom, qMax(170, total - 250));
    ui->mainSplitter->setSizes(QList<int>{total - bottom, bottom});
  }
  // 左右两栏要真正 1:1：setStretchFactor 只在宽裕时生效，一旦某一栏的
  // sizeHint 更大（右侧工具栏带路径标签），Qt 会按 hint 分配。这里直接
  // 显式给等分尺寸，两栏内容再各自省略/滚动，视觉上才是对称的。
  const int wide = ui->splitter->width() - ui->splitter->handleWidth();
  if (wide > 240) {
    const int half = wide / 2;
    ui->splitter->setSizes(QList<int>{half, wide - half});
  }
}

void FileTransferPage::applyRemoteRoots(const QStringList &roots)
{
  // Flux: 下拉框只显示短名（桌面 / 下载 / …），完整路径放在 UserRole 与
  // tooltip 里。旧实现直接显示整串 /storage/media/100/local/files/Docs/xxx，
  // 与右边的 lblRemotePath 同时出现，同一路径写两遍、并挤爆工具栏。
  const QSignalBlocker blocker(ui->cbRemoteRoot);
  ui->cbRemoteRoot->clear();
  for (const QString &root : roots) {
    ui->cbRemoteRoot->addItem(shortRootName(root), root);
    ui->cbRemoteRoot->setItemData(ui->cbRemoteRoot->count() - 1, root, Qt::ToolTipRole);
  }
}

QString FileTransferPage::shortRootName(const QString &path)
{
  const QString trimmed = path.endsWith(QLatin1Char('/')) ? path.chopped(1) : path;
  const QString leaf = trimmed.mid(trimmed.lastIndexOf(QLatin1Char('/')) + 1);
  if (leaf.isEmpty()) {
    return trimmed;
  }
  static const QHash<QString, QString> kNames{
      {QStringLiteral("Desktop"), QStringLiteral("桌面")},
      {QStringLiteral("Download"), QStringLiteral("下载")},
      {QStringLiteral("Documents"), QStringLiteral("文档")},
      {QStringLiteral("Images"), QStringLiteral("图片")},
      {QStringLiteral("Videos"), QStringLiteral("视频")},
      {QStringLiteral("Music"), QStringLiteral("音乐")},
  };
  return kNames.value(leaf, leaf);
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
  m_remotePathLabel = target;
  updateRemotePathElide();
  ui->lblRemotePath->setToolTip(target);
  ui->cbRemoteRoot->setToolTip(target);
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
