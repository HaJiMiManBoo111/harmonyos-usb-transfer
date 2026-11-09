/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClient.h"

#include "common/Constants.h"

#include <QChar>
#include <QString>
#include <QStringList>

namespace deskflow::gui::ipc {

namespace {
/// Separator between the paths of the "sendFiles" command: ASCII unit separator.
constexpr QChar kPathSeparator(0x1f);
} // namespace

CoreIpcClient::CoreIpcClient(QObject *parent) : IpcClient(parent, kCoreIpcName, QStringLiteral("core"))
{
  // do nothing
}

void CoreIpcClient::sendStop()
{
  sendMessage(QStringLiteral("stop"));
}

void CoreIpcClient::sendFiles(const QStringList &paths)
{
  if (paths.isEmpty()) {
    return;
  }

  // the paths are joined with 0x1f because the core splits the message on '='
  sendMessage(QStringLiteral("sendFiles=%1").arg(paths.join(kPathSeparator)));
}

void CoreIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  const auto args = parts.size() >= 2 ? parts.at(1) : QString();
  Q_EMIT commandReceived(command, args);
}

} // namespace deskflow::gui::ipc
