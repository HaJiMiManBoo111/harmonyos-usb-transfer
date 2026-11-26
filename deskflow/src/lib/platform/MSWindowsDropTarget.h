/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>
#include <vector>

namespace deskflow::platform {

//! Native OLE drop source for files dragged in from the Windows shell.
/*!
Registered as an IDropTarget rather than using WM_DROPFILES: only this way does the shell
draw its own drag cursor, and only this way is the drop point (and the modifier keys) given
to us, which the transfer code needs to describe the drop. WM_DROPFILES is also limited to
one window per process and gives no feedback before the mouse button is released.

The COM specific parts live in MSWindowsDropTarget.cpp so that this header stays free of
<oleidl.h> / <shlobj.h>. registerTarget() and unregisterTarget() must both be called from
the thread that owns the window and has OLE initialized -- RegisterDragDrop() requires an
apartment initialized with OleInitialize() on that very thread.
*/
class MSWindowsDropTarget
{
public:
  MSWindowsDropTarget();
  ~MSWindowsDropTarget();

  MSWindowsDropTarget(const MSWindowsDropTarget &) = delete;
  MSWindowsDropTarget &operator=(const MSWindowsDropTarget &) = delete;

  //! Register as the drop target of @p hwnd; false when OLE refused the registration.
  bool registerTarget(HWND hwnd);
  void unregisterTarget();

  //! UTF-8 paths of the most recent drop, and forget them; empty when nothing is pending.
  std::vector<std::string> takeDroppedPaths();

  static constexpr const char *kMimeFormatName = "Flux/FileDrop";

private:
  struct Impl; //!< The IDropTarget implementation; kept out of the header to hide the COM headers.
  Impl *m_impl = nullptr;
};

} // namespace deskflow::platform
