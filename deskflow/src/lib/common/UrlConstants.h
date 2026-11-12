/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

// important: this is used for settings paths on some platforms,
// and must not be a url. qt automatically converts this to reverse domain
// notation (rdn), e.g. org.deskflow
const auto kOrgDomain = QStringLiteral("deskflow.org");

const auto kUrlSourceQuery = QStringLiteral("source=gui");
const auto kUrlApp = QStringLiteral("https://%1").arg(kOrgDomain);
const auto kUrlHelp = QStringLiteral("%1/help?%2").arg(kUrlApp, kUrlSourceQuery);
const auto kUrlDownload = QStringLiteral("%1/download?%2").arg(kUrlApp, kUrlSourceQuery);
const auto kUrlWiki = QStringLiteral("%1/wiki?%2").arg(kUrlApp, kUrlSourceQuery);
// Flux is a fork and has no upstream update server. Keep this empty so the GUI
// never contacts the old deskflow.org endpoint (VersionChecker skips empty URLs).
const auto kUrlUpdateCheck = QStringLiteral("");

#if defined(Q_OS_LINUX)
const auto kUrlGnomeTrayFix = QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/");
#endif
