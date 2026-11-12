/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "HdcProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

#include <algorithm>

namespace deskflow::gui::transfer {

namespace {

// 鸿蒙侧标准目录（与 HdcCore.RemoteDesktop 等常量一致）
const auto kRemoteDesktop = QStringLiteral("/storage/media/100/local/files/Docs/Desktop");
const auto kRemoteDownload = QStringLiteral("/storage/media/100/local/files/Docs/Download");
const auto kRemoteDocuments = QStringLiteral("/storage/media/100/local/files/Docs/Documents");
const auto kRemoteImages = QStringLiteral("/storage/media/100/local/files/Docs/Images");
const auto kRemoteVideos = QStringLiteral("/storage/media/100/local/files/Docs/Videos");
const auto kRemoteMusic = QStringLiteral("/storage/media/100/local/files/Docs/Music");

// 传输过程中的临时目录名（落在本地目标目录下，结束后删除）
const auto kTempDirName = QStringLiteral(".htmp");

constexpr qint64 kEightHoursMs = 8LL * 60 * 60 * 1000;

/// hdc 退出码不可靠（失败时也可能是 0），成败一律看这行输出。
const auto kFinishMarker = QStringLiteral("FileTransfer finish");

QString s_hdcPath;

QString trimmedLeaf(QString path)
{
  while (path.size() > 1 && (path.endsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('\\')))) {
    path.chop(1);
  }
  return path;
}

QString elapsedSecondsText(qint64 ms)
{
  return QString::number(ms / 1000.0, 'f', 1);
}

QString shortenForMessage(QString text)
{
  text.replace(QLatin1Char('\r'), QLatin1Char(' '));
  text.replace(QLatin1Char('\n'), QLatin1Char(' '));
  text = text.trimmed();
  if (text.size() > 160) {
    text = text.left(160);
  }
  return text;
}

} // namespace

HdcProcess::HdcProcess(QObject *parent) : QObject(parent)
{
}

HdcProcess::~HdcProcess() = default;

// ---------- 定位 hdc.exe ----------

QString HdcProcess::baseDir()
{
  const QString appDir = QCoreApplication::applicationDirPath();
  return appDir.isEmpty() ? QDir::currentPath() : appDir;
}

QString HdcProcess::hdcPath()
{
  if (!s_hdcPath.isEmpty()) {
    return s_hdcPath;
  }

  // 1. 程序同目录（便携部署）
  const QString local = QDir(baseDir()).filePath(QStringLiteral("hdc.exe"));
  if (QFileInfo::exists(local)) {
    s_hdcPath = local;
    return s_hdcPath;
  }

  // 2. 环境变量 LINGXI_HDC
  const QString fromEnv = qEnvironmentVariable("LINGXI_HDC");
  if (!fromEnv.isEmpty() && QFileInfo::exists(fromEnv)) {
    s_hdcPath = fromEnv;
    return s_hdcPath;
  }

  // 3. PATH
  const QString fromPath = findOnPath(QStringLiteral("hdc.exe"));
  if (!fromPath.isEmpty()) {
    s_hdcPath = fromPath;
    return s_hdcPath;
  }

  // 4. DevEco Studio 安装目录
  const QString fromDevEco = findInDevEco();
  if (!fromDevEco.isEmpty()) {
    s_hdcPath = fromDevEco;
    return s_hdcPath;
  }

  // 全都找不到时返回程序同目录路径，供上层给出「缺少 hdc」提示
  s_hdcPath = local;
  return s_hdcPath;
}

void HdcProcess::setHdcPath(const QString &path)
{
  s_hdcPath = path;
}

QString HdcProcess::findOnPath(const QString &fileName)
{
  const QString pathVar = qEnvironmentVariable("PATH");
  if (pathVar.isEmpty()) {
    return {};
  }
  const auto dirs = pathVar.split(QDir::listSeparator(), Qt::SkipEmptyParts);
  for (const auto &rawDir : dirs) {
    // PATH 里可能带引号（如 "C:\Program Files\xxx"），去掉首尾引号
    QString dir = rawDir.trimmed();
    if (dir.size() > 1 && dir.startsWith(QLatin1Char('"')) && dir.endsWith(QLatin1Char('"'))) {
      dir = dir.mid(1, dir.size() - 2);
    }
    if (dir.isEmpty()) {
      continue;
    }
    const QString candidate = QDir(dir).filePath(fileName);
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

QString HdcProcess::findInDevEco()
{
  QStringList roots;
  const QString programFiles = qEnvironmentVariable("ProgramFiles");
  const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
  if (!programFiles.isEmpty()) {
    roots << QDir(programFiles).filePath(QStringLiteral("Huawei/DevEco Studio"));
  }
  if (!programFilesX86.isEmpty()) {
    roots << QDir(programFilesX86).filePath(QStringLiteral("Huawei/DevEco Studio"));
  }
  roots << QStringLiteral("C:/Program Files/Huawei/DevEco Studio");

  QString best;
  for (const auto &root : roots) {
    if (!QFileInfo::exists(root)) {
      continue;
    }
    const QString direct =
        QDir(root).filePath(QStringLiteral("sdk/default/openharmony/toolchains/hdc.exe"));
    if (QFileInfo::exists(direct)) {
      best = direct;
    }
    if (!best.isEmpty()) {
      break;
    }
    const QDir sdkDir(QDir(root).filePath(QStringLiteral("sdk")));
    const auto versions = sdkDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &version : versions) {
      const QString candidate =
          sdkDir.filePath(version + QStringLiteral("/openharmony/toolchains/hdc.exe"));
      if (QFileInfo::exists(candidate)) {
        best = candidate;
      }
    }
  }
  return best;
}

// ---------- 设备 ----------

QList<HdcDevice> HdcProcess::listDevices()
{
  QList<HdcDevice> devices;
  if (!QFileInfo::exists(hdcPath())) {
    return devices;
  }

  const ProcessResult result = runProcess({QStringLiteral("list"), QStringLiteral("targets"), QStringLiteral("-v")},
                                          15000);

  static const QRegularExpression usbRe(R"(^\s*(\S+)\s+USB\s+Connected)",
                                       QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression tcpRe(R"(^\s*(\S+)\s+TCP\s+Connected)",
                                        QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression connRe(R"(^\s*(\S+)\s+Connected)", QRegularExpression::CaseInsensitiveOption);

  const auto lines = cleanLines(result.standardOutput);
  for (const auto &line : lines) {
    if (line.trimmed().isEmpty()) {
      continue;
    }

    auto match = usbRe.match(line);
    if (match.hasMatch()) {
      devices.append(HdcDevice{match.captured(1), true});
      continue;
    }

    match = tcpRe.match(line);
    if (match.hasMatch()) {
      devices.append(HdcDevice{match.captured(1), false});
      continue;
    }

    match = connRe.match(line);
    if (match.hasMatch()) {
      const QString serial = match.captured(1);
      // 串口（COMx）不是可用设备，跳过
      if (serial.startsWith(QStringLiteral("COM"), Qt::CaseInsensitive)) {
        continue;
      }
      devices.append(HdcDevice{serial, false});
    }
  }
  return devices;
}

bool HdcProcess::pickDevice(HdcDevice *device)
{
  if (device == nullptr) {
    return false;
  }
  const auto devices = listDevices();
  for (const auto &candidate : devices) {
    if (candidate.isUsb) {
      *device = candidate;
      return true;
    }
  }
  if (!devices.isEmpty()) {
    *device = devices.first();
    return true;
  }
  return false;
}

QString HdcProcess::resolveDevice()
{
  const auto devices = listDevices();
  if (devices.size() == 1) {
    return devices.first().serial;
  }
  return {};
}

QString HdcProcess::deviceLabel(const HdcDevice &device)
{
  const QString kind = device.isUsb ? QStringLiteral("USB") : QStringLiteral("网络");
  return QStringLiteral("鸿蒙设备 (%1) %2").arg(kind, device.serial);
}

// ---------- 远程 shell 辅助 ----------

QStringList HdcProcess::remoteRoots() const
{
  return {kRemoteDesktop, kRemoteDownload, kRemoteDocuments, kRemoteImages, kRemoteVideos, kRemoteMusic};
}

QString HdcProcess::sq(const QString &value)
{
  // 远程 shell 单引号转义：把 ' 变成 '\''
  if (value.isEmpty()) {
    return QStringLiteral("''");
  }
  QString escaped = value;
  escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
  return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString HdcProcess::remoteShell(const QString &serial, const QString &command, int timeoutMs)
{
  const ProcessResult result =
      runProcess({QStringLiteral("-t"), serial, QStringLiteral("shell"), command}, timeoutMs);
  return result.standardOutput;
}

QStringList HdcProcess::cleanLines(const QString &stdOut)
{
  QString normalized = stdOut;
  normalized.remove(QLatin1Char('\r'));
  QStringList lines;
  const auto raw = normalized.split(QLatin1Char('\n'));
  for (const auto &line : raw) {
    if (line.isEmpty()) {
      continue;
    }
    if (line.startsWith(QStringLiteral("find: "))) {
      continue;
    }
    lines.append(line);
  }
  return lines;
}

qint64 HdcProcess::remoteSize(const QString &serial, const QString &remotePath)
{
  // 参考实现这里没给路径加引号（含空格的路径会让 stat 失败）。移植时补上 sq()，
  // 只影响进度轮询的精度，不影响传输本身。
  const QString command = QStringLiteral("stat -c %s %1 2>/dev/null").arg(sq(remotePath));
  const QString out = remoteShell(serial, command, 10000);

  QString text = out;
  text.remove(QLatin1Char('\r'));
  text.remove(QLatin1Char('\n'));
  bool ok = false;
  const qint64 value = text.trimmed().toLongLong(&ok);
  return ok ? value : -1;
}

qint64 HdcProcess::remoteDirSizeFor(const QString &serial, const QString &remotePath)
{
  // 不用 du -sb：它统计磁盘占用（含目录 inode 与块对齐），8 字节的目录会报 6888。
  const QString command =
      QStringLiteral("find %1 -type f -printf '%s\\n' 2>/dev/null | awk '{s+=$1} END {print s+0}'")
          .arg(sq(remotePath));
  const auto lines = cleanLines(remoteShell(serial, command, 180000));
  if (lines.isEmpty()) {
    return 0;
  }
  bool ok = false;
  const qint64 value = lines.first().trimmed().toLongLong(&ok);
  return ok ? value : 0;
}

qint64 HdcProcess::remoteDirSize(const QString &remotePath)
{
  HdcDevice device;
  if (!pickDevice(&device)) {
    return 0;
  }
  return remoteDirSizeFor(device.serial, remotePath);
}

void HdcProcess::probeRemoteKind(const QString &serial, const QString &remotePath, bool *isDir, qint64 *size)
{
  if (isDir != nullptr) {
    *isDir = false;
  }
  if (size != nullptr) {
    *size = 0;
  }

  const QString command = QStringLiteral("stat -c '%F|%s' %1 2>/dev/null").arg(sq(remotePath));
  const auto lines = cleanLines(remoteShell(serial, command, 30000));
  if (lines.isEmpty()) {
    return;
  }

  const QString line = lines.first();
  const int bar = line.indexOf(QLatin1Char('|'));
  const QString kind = bar > 0 ? line.left(bar).trimmed() : line.trimmed();
  const QString num = bar > 0 ? line.mid(bar + 1) : QString();

  bool ok = false;
  const qint64 parsed = num.trimmed().toLongLong(&ok);

  if (kind.compare(QStringLiteral("directory"), Qt::CaseInsensitive) == 0) {
    if (isDir != nullptr) {
      *isDir = true;
    }
    if (size != nullptr) {
      *size = remoteDirSizeFor(serial, remotePath);
    }
    // 目录的精确大小要递归统计
  } else if (size != nullptr) {
    *size = ok ? parsed : 0;
  }
}

QList<HdcRemoteItem> HdcProcess::listRemote(const QString &remoteDir)
{
  QList<HdcRemoteItem> items;

  HdcDevice device;
  if (!pickDevice(&device)) {
    Q_EMIT logLine(QStringLiteral("[失败] 未找到鸿蒙设备，无法读取远程目录"));
    return items;
  }

  QString dir = remoteDir;
  if (dir.isEmpty()) {
    dir = kRemoteDesktop;
  }
  if (dir.size() > 1) {
    while (dir.endsWith(QLatin1Char('/'))) {
      dir.chop(1);
    }
  }

  // 目录不存在时必须报错，不能返回空列表——否则界面会把「路径写错」显示成「这个目录是空的」。
  // 注意：不能用 contains("directory") 判断，stat 的错误串 "No such file or directory" 里也有这个词。
  // 鸿蒙 hdc shell 以 uid=2000(shell) 运行，stat '/' 返回
  // "Permission denied"，根目录本身无法列出。把 '/' 作为「虚拟根」，
  // 直接给出 6 个可读的标准目录，双击即可进入；「上级」回到 '/' 也能用。
  if (dir == QStringLiteral("/")) {
    for (const auto &root : remoteRoots()) {
      HdcRemoteItem item;
      item.name = QFileInfo(root).fileName();
      item.fullPath = root;
      item.isDir = true;
      item.size = 0;
      items.append(item);
    }
    return items;
  }

  const QString kindCmd = QStringLiteral("stat -c '%F' %1 2>&1").arg(sq(dir));
  const auto kindLines = cleanLines(remoteShell(device.serial, kindCmd, 30000));
  const QString kindText = kindLines.isEmpty() ? QString() : kindLines.first().trimmed();
  if (kindText.compare(QStringLiteral("directory"), Qt::CaseInsensitive) != 0) {
    const QString reason = kindText.isEmpty() ? QStringLiteral("目录不存在或没有权限") : kindText;
    Q_EMIT logLine(QStringLiteral("无法读取鸿蒙目录 %1：%2").arg(dir, reason));
    return items;
  }

  // 1) 普通文件：%s 大小 \t %f 文件名
  const QString cmdF =
      QStringLiteral("find %1 -mindepth 1 -maxdepth 1 -type f -printf '%s\\t%f\\n' 2>/dev/null").arg(sq(dir));
  const auto fileLines = cleanLines(remoteShell(device.serial, cmdF, 60000));
  for (const auto &line : fileLines) {
    const int tab = line.indexOf(QLatin1Char('\t'));
    if (tab <= 0) {
      continue;
    }
    const QString num = line.left(tab);
    const QString name = line.mid(tab + 1);
    if (name.isEmpty()) {
      continue;
    }
    bool ok = false;
    const qint64 size = num.trimmed().toLongLong(&ok);
    HdcRemoteItem item;
    item.name = name;
    item.fullPath = joinRemote(dir, name);
    item.isDir = false;
    item.size = ok ? size : 0;
    items.append(item);
  }

  // 2) 子目录名字（类型要单独 find，设备侧不支持 -printf %y）
  const QString cmdD =
      QStringLiteral("find %1 -mindepth 1 -maxdepth 1 -type d -printf '%f\\n' 2>/dev/null").arg(sq(dir));
  const auto dirLines = cleanLines(remoteShell(device.serial, cmdD, 60000));
  QStringList dirNames;
  for (const auto &line : dirLines) {
    const QString name = line.trimmed();
    if (name.isEmpty()) {
      continue;
    }
    dirNames.append(name);
  }

  if (!dirNames.isEmpty()) {
    // 每个子目录的递归文件字节数：一次 find 拿「大小 + 相对路径」，在本地按第一段累加。
    // 不在设备上 awk：整条命令要塞进 hdc 的 shell 里，内部再出现双引号会把外层引用拆坏。
    QHash<QString, qint64> sizes;
    const QString cmdS = QStringLiteral("find %1 -type f -printf '%s\\t%P\\n' 2>/dev/null").arg(sq(dir));
    const auto sizeLines = cleanLines(remoteShell(device.serial, cmdS, 180000));
    for (const auto &line : sizeLines) {
      const int tab = line.indexOf(QLatin1Char('\t'));
      if (tab <= 0) {
        continue;
      }
      bool ok = false;
      const qint64 size = line.left(tab).trimmed().toLongLong(&ok);
      if (!ok) {
        continue;
      }
      const QString rel = line.mid(tab + 1);
      const int slash = rel.indexOf(QLatin1Char('/'));
      if (slash <= 0) {
        continue; // 落在目录根下的散文件，不计入任何子目录
      }
      sizes[rel.left(slash)] += size;
    }

    for (const auto &name : dirNames) {
      HdcRemoteItem item;
      item.name = name;
      item.fullPath = joinRemote(dir, name);
      item.isDir = true;
      item.size = sizes.value(name, 0);
      items.append(item);
    }
  }

  std::sort(items.begin(), items.end(), [](const HdcRemoteItem &a, const HdcRemoteItem &b) {
    if (a.isDir != b.isDir) {
      return a.isDir;
    }
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });
  return items;
}

QString HdcProcess::joinRemote(const QString &dir, const QString &name)
{
  if (dir.endsWith(QLatin1Char('/'))) {
    return dir + name;
  }
  return dir + QLatin1Char('/') + name;
}

QString HdcProcess::lastSegment(const QString &path)
{
  if (path.isEmpty()) {
    return {};
  }
  QString trimmed = path;
  while (trimmed.endsWith(QLatin1Char('/'))) {
    trimmed.chop(1);
  }
  const int slash = trimmed.lastIndexOf(QLatin1Char('/'));
  return slash >= 0 ? trimmed.mid(slash + 1) : trimmed;
}

qint64 HdcProcess::recursiveSize(const QString &path)
{
  const QFileInfo info(path);
  if (!info.exists()) {
    return 0;
  }
  if (info.isFile()) {
    return info.size();
  }
  if (!info.isDir()) {
    return 0;
  }
  qint64 total = 0;
  QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    total += it.fileInfo().size();
  }
  return total;
}

QString HdcProcess::defaultPullDir()
{
  QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
  if (desktop.isEmpty()) {
    desktop = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  }
  return QDir(desktop).filePath(QStringLiteral("from-harmony"));
}

QString HdcProcess::destLabel(const QString &localDir)
{
  if (localDir.isEmpty()) {
    return QStringLiteral("本地目录");
  }
  const QString trimmed = trimmedLeaf(localDir);
  const QString leaf = QFileInfo(trimmed).fileName();
  return leaf.isEmpty() ? trimmed : leaf;
}

// ---------- 进程调用 ----------

HdcProcess::ProcessResult HdcProcess::runProcess(
    const QStringList &args, int timeoutMs, int tickMs, const std::function<void()> &onTick, bool streamLog
)
{
  ProcessResult result;

  QProcess process;
  process.setProgram(hdcPath());
  process.setArguments(args);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(QIODevice::ReadOnly);
  if (!process.waitForStarted(15000)) {
    result.started = false;
    result.standardError = process.errorString();
    return result;
  }
  result.started = true;

  QByteArray outBytes;
  QByteArray errBytes;
  QByteArray pendingLine;

  auto drainOutput = [&]() {
    const QByteArray chunk = process.readAllStandardOutput();
    if (chunk.isEmpty()) {
      return;
    }
    outBytes += chunk;
    if (!streamLog) {
      return;
    }
    pendingLine += chunk;
    int newline = pendingLine.indexOf('\n');
    while (newline >= 0) {
      QByteArray raw = pendingLine.left(newline);
      pendingLine.remove(0, newline + 1);
      while (raw.endsWith('\r')) {
        raw.chop(1);
      }
      const QString line = QString::fromUtf8(raw);
      if (!line.isEmpty()) {
        Q_EMIT logLine(line);
      }
      newline = pendingLine.indexOf('\n');
    }
  };
  auto drainError = [&]() { errBytes += process.readAllStandardError(); };

  QElapsedTimer totalTimer;
  totalTimer.start();
  QElapsedTimer tickTimer;
  tickTimer.start();

  constexpr int kSliceMs = 100;
  bool finished = false;
  while (!finished) {
    finished = process.waitForFinished(kSliceMs);
    drainOutput();
    drainError();
    if (finished) {
      break;
    }

    if (tickMs > 0 && onTick && tickTimer.elapsed() >= tickMs) {
      tickTimer.restart();
      onTick();
    }

    const qint64 elapsed = totalTimer.elapsed();
    if ((timeoutMs > 0 && elapsed > timeoutMs) || elapsed > kEightHoursMs) {
      result.timedOut = true;
      process.kill();
      process.waitForFinished(3000);
      drainOutput();
      drainError();
      break;
    }
  }

  if (streamLog && !pendingLine.isEmpty()) {
    QByteArray raw = pendingLine;
    pendingLine.clear();
    while (raw.endsWith('\r')) {
      raw.chop(1);
    }
    const QString line = QString::fromUtf8(raw);
    if (!line.isEmpty()) {
      Q_EMIT logLine(line);
    }
  }

  result.standardOutput = QString::fromUtf8(outBytes);
  if (result.timedOut) {
    result.exitCode = -1;
    result.standardError = QStringLiteral("timeout");
  } else {
    result.exitCode = process.exitCode();
    result.standardError = QString::fromUtf8(errBytes);
  }
  return result;
}

HdcProcess::ProcessResult HdcProcess::sendOneFile(
    const QString &serial, const QString &localPath, const QString &remoteTarget, qint64 localSize, qint64 sentBefore,
    qint64 grandTotal, const QString &currentFile
)
{
  qint64 lastSeen = -1;

  auto onTick = [&]() {
    if (localSize <= 0) {
      return;
    }
    const qint64 remoteBytes = remoteSize(serial, remoteTarget);
    if (remoteBytes <= lastSeen) {
      return;
    }
    lastSeen = remoteBytes;
    qint64 done = sentBefore + remoteBytes;
    const qint64 total = grandTotal > 0 ? grandTotal : localSize;
    if (done > total) {
      done = total;
    }
    Q_EMIT progress(done, total, currentFile);
  };

  return runProcess({QStringLiteral("-t"), serial, QStringLiteral("file"), QStringLiteral("send"), localPath,
                     remoteTarget},
                    0, 700, onTick, true);
}

HdcProcess::ProcessResult HdcProcess::recvWithProgress(
    const QStringList &args, const QString &watchPath, qint64 itemTotal, qint64 doneBefore, qint64 grandTotal,
    const QString &currentFile
)
{
  qint64 lastSeen = -1;

  auto onTick = [&]() {
    const qint64 seen = recursiveSize(watchPath);
    if (seen <= lastSeen) {
      return;
    }
    lastSeen = seen;
    const qint64 total = grandTotal > 0 ? grandTotal : itemTotal;
    qint64 done = doneBefore + seen;
    if (total > 0 && done > total) {
      done = total;
    }
    Q_EMIT progress(done, total > 0 ? total : 0, currentFile);
  };

  return runProcess(args, 0, 500, onTick, true);
}

// ---------- 发送 ----------

bool HdcProcess::hasFinishMarker(const ProcessResult &result)
{
  return result.standardOutput.contains(kFinishMarker, Qt::CaseInsensitive);
}

HdcTransferResult HdcProcess::send(const QStringList &localPaths, const QString &remoteDir)
{
  HdcTransferResult result;
  QElapsedTimer timer;
  timer.start();

  if (localPaths.isEmpty()) {
    result.message = QStringLiteral("没有要传的文件。");
    return result;
  }

  QStringList valid;
  QStringList missing;
  for (const auto &path : localPaths) {
    if (QFileInfo::exists(path)) {
      valid.append(path);
    } else {
      missing.append(path);
    }
  }
  if (valid.isEmpty()) {
    result.message = QStringLiteral("选中的路径都不存在。");
    return result;
  }

  QString targetDir = remoteDir;
  if (targetDir.isEmpty()) {
    targetDir = kRemoteDesktop;
  }
  if (!targetDir.endsWith(QLatin1Char('/'))) {
    targetDir += QLatin1Char('/');
  }

  result.fileCount = valid.size();

  qint64 grandTotal = 0;
  for (const auto &path : valid) {
    grandTotal += recursiveSize(path);
  }
  result.totalBytes = grandTotal;

  if (!QFileInfo::exists(hdcPath())) {
    result.message = QStringLiteral("缺少 hdc.exe。请把它放到本程序目录，或安装 DevEco Studio，或设置 LINGXI_HDC。");
    result.elapsedMs = timer.elapsed();
    return result;
  }

  HdcDevice device;
  if (!pickDevice(&device)) {
    result.message = QStringLiteral("未找到鸿蒙设备。请用数据线连接电脑，并在鸿蒙上开启 USB 调试后重试。");
    result.elapsedMs = timer.elapsed();
    return result;
  }
  Q_EMIT logLine(QStringLiteral("[设备] ") + deviceLabel(device));

  qint64 sentBefore = 0;
  int okCount = 0;
  QStringList failures;

  for (const auto &local : valid) {
    const bool isDir = QFileInfo(local).isDir();
    QString name = QFileInfo(trimmedLeaf(local)).fileName();
    if (name.isEmpty()) {
      name = local;
    }
    const QString remoteTarget = targetDir + name;
    const qint64 localSize = recursiveSize(local);

    Q_EMIT logLine(QStringLiteral("[发送] %1 -> %2").arg(name, remoteTarget));

    ProcessResult runResult;
    if (isDir) {
      // 目录：进度不确定，先报一次「已传字节 / 0」
      Q_EMIT progress(sentBefore, 0, name);
      // 传目录时目标给的是远程父目录，hdc 会在其下新建同名目录
      runResult = runProcess({QStringLiteral("-t"), device.serial, QStringLiteral("file"),
                              QStringLiteral("send"), local, targetDir},
                             3600000);
    } else {
      runResult = sendOneFile(device.serial, local, remoteTarget, localSize, sentBefore, grandTotal, name);
    }

    if (hasFinishMarker(runResult)) {
      okCount++;
      sentBefore += localSize;
      Q_EMIT progress(sentBefore, grandTotal > 0 ? grandTotal : 0, name);
    } else {
      QString error = runResult.standardOutput;
      if (!runResult.standardError.trimmed().isEmpty()) {
        error = error + QLatin1Char(' ') + runResult.standardError;
      }
      error = shortenForMessage(error);
      failures.append(QStringLiteral("%1: %2").arg(name, error.isEmpty() ? QStringLiteral("传输失败") : error));
      Q_EMIT logLine(QStringLiteral("[失败] %1 %2").arg(name, error));
    }
  }

  result.elapsedMs = timer.elapsed();

  if (failures.isEmpty()) {
    result.success = true;
    const QString dest = targetDir == kRemoteDesktop + QLatin1Char('/') ? QStringLiteral("鸿蒙桌面") : targetDir;
    result.message = QStringLiteral("已成功传到%1：%2 项，%3，耗时 %4 秒（%5）")
                         .arg(dest)
                         .arg(okCount)
                         .arg(formatBytes(sentBefore), elapsedSecondsText(result.elapsedMs),
                              formatSpeed(sentBefore, result.elapsedMs));
    if (!missing.isEmpty()) {
      result.message += QStringLiteral("（跳过 %1 个不存在的路径）").arg(missing.size());
    }
  } else {
    result.success = okCount > 0;
    result.message =
        QStringLiteral("成功 %1 项，失败 %2 项。首个错误：%3").arg(okCount).arg(failures.size()).arg(failures.first());
    if (okCount > 0) {
      result.message += QStringLiteral("（已成功的那部分已在设备上）");
    }
  }
  return result;
}

// ---------- 接收 ----------

bool HdcProcess::pullOne(
    const QString &serial, const QString &remotePath, const QString &name, const QString &localDir,
    const QString &tmpRoot, qint64 itemTotal, qint64 doneBefore, qint64 grandTotal, QString *error
)
{
  if (error != nullptr) {
    error->clear();
  }

  if (!QDir().mkpath(tmpRoot)) {
    if (error != nullptr) {
      *error = QStringLiteral("无法创建临时目录 %1").arg(tmpRoot);
    }
    return false;
  }

  // 清掉可能残留的同名临时项
  const QString tmpItem = QDir(tmpRoot).filePath(name);
  const QFileInfo tmpInfo(tmpItem);
  if (tmpInfo.exists()) {
    if (tmpInfo.isDir()) {
      QDir(tmpItem).removeRecursively();
    } else {
      QFile::remove(tmpItem);
    }
  }

  const QStringList args{QStringLiteral("-t"), serial, QStringLiteral("file"), QStringLiteral("recv"), remotePath,
                         tmpRoot};
  const ProcessResult runResult = recvWithProgress(args, tmpItem, itemTotal, doneBefore, grandTotal, name);

  if (!hasFinishMarker(runResult)) {
    QString text = runResult.standardOutput;
    if (!runResult.standardError.trimmed().isEmpty()) {
      text = text + QLatin1Char(' ') + runResult.standardError;
    }
    if (error != nullptr) {
      *error = shortenForMessage(text);
    }
    return false;
  }

  // 落地：临时项 -> 正式目标
  const QString finalPath = QDir(localDir).filePath(name);
  bool moved = false;
  for (int attempt = 0; attempt < 3 && !moved; ++attempt) {
    const QFileInfo finalInfo(finalPath);
    if (finalInfo.exists()) {
      if (finalInfo.isDir()) {
        QDir(finalPath).removeRecursively();
      } else {
        QFile::remove(finalPath);
      }
    }

    const QFileInfo movedInfo(tmpItem);
    bool ok = false;
    if (movedInfo.isDir()) {
      ok = QDir().rename(tmpItem, finalPath);
    } else if (movedInfo.isFile()) {
      ok = QFile::rename(tmpItem, finalPath);
    } else {
      if (error != nullptr) {
        *error = QStringLiteral("传输完成但本地没有产生文件（可能被占用或磁盘权限不足）");
      }
      return false;
    }

    if (ok) {
      moved = true;
    } else if (attempt == 2) {
      if (error != nullptr) {
        *error = QStringLiteral("落地失败：无法把 %1 移动到 %2").arg(tmpItem, finalPath);
      }
      return false;
    } else {
      QThread::msleep(300);
    }
  }
  return moved;
}

HdcTransferResult HdcProcess::pull(const QStringList &remotePaths, const QString &localDir)
{
  HdcTransferResult result;
  QElapsedTimer timer;
  timer.start();

  if (remotePaths.isEmpty()) {
    result.message = QStringLiteral("没有选中要接收的文件。");
    return result;
  }

  QString targetDir = localDir;
  if (targetDir.isEmpty()) {
    targetDir = defaultPullDir();
  }

  result.fileCount = remotePaths.size();

  if (!QFileInfo::exists(hdcPath())) {
    result.message = QStringLiteral("缺少 hdc.exe。请把它放到本程序目录，或安装 DevEco Studio，或设置 LINGXI_HDC。");
    result.elapsedMs = timer.elapsed();
    return result;
  }

  HdcDevice device;
  if (!pickDevice(&device)) {
    result.message = QStringLiteral("未找到鸿蒙设备。请用数据线连接电脑，并在鸿蒙上开启 USB 调试后重试。");
    result.elapsedMs = timer.elapsed();
    return result;
  }
  Q_EMIT logLine(QStringLiteral("[设备] ") + deviceLabel(device));

  if (!QDir().mkpath(targetDir)) {
    result.message = QStringLiteral("无法创建本地目录 %1：请检查路径是否可写").arg(targetDir);
    result.elapsedMs = timer.elapsed();
    return result;
  }
  Q_EMIT logLine(QStringLiteral("[目标] ") + targetDir);

  // 预扫描：类型 + 大小
  QStringList names;
  qint64 grandTotal = 0;
  for (const auto &remotePath : remotePaths) {
    if (remotePath.isEmpty()) {
      names.append(QString());
      continue;
    }
    bool isDir = false;
    qint64 size = 0;
    probeRemoteKind(device.serial, remotePath, &isDir, &size);
    names.append(lastSegment(remotePath));
    grandTotal += size;
  }
  result.totalBytes = grandTotal;

  const QString tmpRoot = QDir(targetDir).filePath(kTempDirName);
  qint64 doneBefore = 0;
  int okCount = 0;
  QStringList failures;

  for (int i = 0; i < remotePaths.size(); ++i) {
    const QString remotePath = remotePaths.at(i);
    const QString name = names.at(i);
    if (name.isEmpty()) {
      failures.append(QStringLiteral("%1: 路径无效").arg(remotePath));
      continue;
    }

    Q_EMIT logLine(QStringLiteral("[接收] %1 -> %2").arg(remotePath, QDir(targetDir).filePath(name)));

    // 单次进度需要一个总量：再探一次（与参考实现一致）
    bool isDir = false;
    qint64 itemTotal = 0;
    probeRemoteKind(device.serial, remotePath, &isDir, &itemTotal);

    QString error;
    if (pullOne(device.serial, remotePath, name, targetDir, tmpRoot, itemTotal, doneBefore, grandTotal, &error)) {
      okCount++;
      doneBefore += itemTotal;
      Q_EMIT progress(doneBefore, grandTotal > 0 ? grandTotal : 0, name);
    } else {
      failures.append(QStringLiteral("%1: %2").arg(name, error.isEmpty() ? QStringLiteral("接收失败") : error));
      Q_EMIT logLine(QStringLiteral("[失败] %1 %2").arg(name, error));
    }
  }

  QDir(tmpRoot).removeRecursively();

  result.elapsedMs = timer.elapsed();

  if (failures.isEmpty()) {
    result.success = true;
    result.message = QStringLiteral("已成功收到%1：%2 项，%3，耗时 %4 秒（%5）")
                         .arg(destLabel(targetDir))
                         .arg(okCount)
                         .arg(formatBytes(grandTotal), elapsedSecondsText(result.elapsedMs),
                              formatSpeed(grandTotal, result.elapsedMs));
  } else {
    result.success = okCount > 0;
    result.message =
        QStringLiteral("成功 %1 项，失败 %2 项。首个错误：%3").arg(okCount).arg(failures.size()).arg(failures.first());
  }
  return result;
}

// ---------- 格式化 ----------

QString HdcProcess::formatBytes(qint64 bytes)
{
  if (bytes < 1024) {
    return QString::number(bytes) + QStringLiteral(" B");
  }
  if (bytes < 1024LL * 1024) {
    return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
  }
  if (bytes < 1024LL * 1024 * 1024) {
    return QString::number(bytes / (1024.0 * 1024), 'f', 1) + QStringLiteral(" MB");
  }
  return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + QStringLiteral(" GB");
}

QString HdcProcess::formatSpeed(qint64 bytes, qint64 ms)
{
  const double bps = (bytes <= 0 || ms <= 0) ? 0.0 : bytes * 1000.0 / ms;
  if (bps <= 0) {
    return QStringLiteral("-");
  }
  if (bps < 1024) {
    return QString::number(bps, 'f', 0) + QStringLiteral(" B/s");
  }
  if (bps < 1024 * 1024) {
    return QString::number(bps / 1024.0, 'f', 1) + QStringLiteral(" KB/s");
  }
  return QString::number(bps / (1024.0 * 1024), 'f', 1) + QStringLiteral(" MB/s");
}

} // namespace deskflow::gui::transfer
