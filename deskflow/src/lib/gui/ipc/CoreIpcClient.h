/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcClient.h"

#include <QObject>
#include <QStringList>

namespace deskflow::gui::ipc {

class CoreIpcClient : public IpcClient
{
  Q_OBJECT

public:
  explicit CoreIpcClient(QObject *parent = nullptr);

  void sendStop();

  /**
   * @brief Ask the core to send the given local files to the connected client.
   *
   * The core parses every IPC message with `message.split('=')`, so the
   * parameter part must not contain another '='. The paths are therefore
   * joined with the ASCII unit separator (0x1F) which cannot appear in a
   * Windows path, so a path containing '=' stays intact.
   */
  void sendFiles(const QStringList &paths);

Q_SIGNALS:
  void commandReceived(const QString &command, const QString &args);

protected:
  void processCommand(const QString &command, const QStringList &parts) override;
};

} // namespace deskflow::gui::ipc
