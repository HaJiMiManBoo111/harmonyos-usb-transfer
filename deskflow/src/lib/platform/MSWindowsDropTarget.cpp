/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsDropTarget.h"

#include "base/Log.h"

#include <Ole2.h>

#include <shellapi.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace deskflow::platform {

namespace {

//! CF_HDROP is the shell's "list of dropped files" clipboard format. CFSTR_FILEDESCRIPTOR
//! would only be needed for virtual files copied out of a container, and Deskflow transfers
//! real files from disk, so the plain CF_HDROP path is enough.
constexpr CLIPFORMAT kClipboardFormatHdrop = CF_HDROP;

std::string wideToUtf8(const std::wstring &wide)
{
  if (wide.empty()) {
    return std::string();
  }

  const int size = ::WideCharToMultiByte(
      CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr
  );
  if (size <= 0) {
    return std::string();
  }

  std::string utf8(static_cast<std::size_t>(size), '\0');
  ::WideCharToMultiByte(
      CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), size, nullptr, nullptr
  );
  return utf8;
}

} // namespace

//
// MSWindowsDropTarget::Impl
//

struct MSWindowsDropTarget::Impl : public IDropTarget
{
  Impl()
  {
    // A drop target only has to work while the target window exists; both are created and
    // destroyed on the main thread, so no lock is needed around m_hwnd. S_FALSE means the
    // apartment was already initialized by someone else (MSWindowsComputer does it for the
    // clipboard), and then we must not call OleUninitialize() -- hence m_oleInitialized.
    const HRESULT result = ::OleInitialize(nullptr);
    m_oleInitialized = SUCCEEDED(result);
    if (!m_oleInitialized) {
      LOG_WARN("could not initialize ole for the file drop target, error: 0x%08lx", result);
    }
  }

  ~Impl()
  {
    unregisterTarget();
    if (m_oleInitialized) {
      ::OleUninitialize();
    }
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
  {
    if (out == nullptr) {
      return E_INVALIDARG;
    }
    *out = nullptr;

    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IDropTarget)) {
      *out = static_cast<IDropTarget *>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override
  {
    return static_cast<ULONG>(++m_refCount);
  }

  ULONG STDMETHODCALLTYPE Release() override
  {
    // The owner (MSWindowsDropTarget) holds one reference for its own lifetime; the shell
    // holds others only while a drag is in progress, so the object is never deleted here.
    const long remaining = --m_refCount;
    if (remaining <= 0) {
      m_refCount = 0;
    }
    return static_cast<ULONG>(remaining < 0 ? 0 : remaining);
  }

  // IDropTarget
  HRESULT STDMETHODCALLTYPE
  DragEnter(IDataObject *dataObject, DWORD /*keyState*/, POINTL /*point*/, DWORD *effect) override
  {
    return setDropEffect(dataObject, effect);
  }

  HRESULT STDMETHODCALLTYPE DragOver(DWORD /*keyState*/, POINTL /*point*/, DWORD *effect) override
  {
    // No data object is offered here; reuse the answer from DragEnter so the cursor stays
    // correct for the whole drag.
    if (effect != nullptr) {
      *effect = m_lastEffect;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DragLeave() override
  {
    m_lastEffect = DROPEFFECT_NONE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  Drop(IDataObject *dataObject, DWORD /*keyState*/, POINTL /*point*/, DWORD *effect) override
  {
    if (effect != nullptr) {
      *effect = DROPEFFECT_NONE;
    }
    if (dataObject == nullptr) {
      return E_INVALIDARG;
    }

    FORMATETC format = hdropFormat();
    STGMEDIUM medium = {};
    const HRESULT result = dataObject->GetData(&format, &medium);
    if (FAILED(result)) {
      LOG_DEBUG("dropped data has no file list, error: 0x%08lx", result);
      return result;
    }

    std::vector<std::string> paths;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr) {
      const auto drop = static_cast<HDROP>(::GlobalLock(medium.hGlobal));
      if (drop != nullptr) {
        const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i) {
          const UINT length = ::DragQueryFileW(drop, i, nullptr, 0);
          if (length == 0) {
            continue;
          }
          std::wstring wide(static_cast<std::size_t>(length) + 1, L'\0');
          if (::DragQueryFileW(drop, i, wide.data(), length + 1) == 0) {
            continue;
          }
          wide.resize(length);
          paths.push_back(wideToUtf8(wide));
        }
        ::GlobalUnlock(medium.hGlobal);
      }
    }
    ::ReleaseStgMedium(&medium);

    if (paths.empty()) {
      LOG_DEBUG("drop contained no usable file paths");
      m_lastEffect = DROPEFFECT_NONE;
      return S_OK;
    }

    LOG_DEBUG("received %d dropped file(s)", static_cast<int>(paths.size()));
    {
      std::scoped_lock lock{m_mutex};
      m_droppedPaths = std::move(paths);
    }
    if (effect != nullptr) {
      *effect = DROPEFFECT_COPY;
    }
    m_lastEffect = DROPEFFECT_COPY;
    return S_OK;
  }

  bool registerTarget(HWND hwnd)
  {
    if (hwnd == nullptr) {
      LOG_ERR("cannot register the file drop target without a window");
      return false;
    }
    if (m_hwnd != nullptr) {
      unregisterTarget();
    }

    const HRESULT result = ::RegisterDragDrop(hwnd, this);
    if (FAILED(result)) {
      LOG_ERR("could not register the file drop target, error: 0x%08lx", result);
      return false;
    }

    m_hwnd = hwnd;
    LOG_DEBUG("registered the file drop target on window %p", static_cast<void *>(hwnd));
    return true;
  }

  void unregisterTarget()
  {
    if (m_hwnd == nullptr) {
      return;
    }

    const HRESULT result = ::RevokeDragDrop(m_hwnd);
    if (FAILED(result)) {
      LOG_DEBUG("could not revoke the file drop target, error: 0x%08lx", result);
    }
    m_hwnd = nullptr;
  }

  std::vector<std::string> takeDroppedPaths()
  {
    std::scoped_lock lock{m_mutex};
    std::vector<std::string> paths;
    paths.swap(m_droppedPaths);
    return paths;
  }

private:
  static FORMATETC hdropFormat()
  {
    FORMATETC format = {};
    format.cfFormat = kClipboardFormatHdrop;
    format.ptd = nullptr;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    return format;
  }

  HRESULT setDropEffect(IDataObject *dataObject, DWORD *effect)
  {
    DWORD dropEffect = DROPEFFECT_NONE;
    if (dataObject != nullptr) {
      FORMATETC format = hdropFormat();
      if (dataObject->QueryGetData(&format) == S_OK) {
        dropEffect = DROPEFFECT_COPY;
      }
    }

    if (effect != nullptr) {
      *effect = dropEffect;
    }
    m_lastEffect = dropEffect;
    return S_OK;
  }

  std::atomic<long> m_refCount{0};
  bool m_oleInitialized = false;
  HWND m_hwnd = nullptr;
  DWORD m_lastEffect = DROPEFFECT_NONE;

  std::mutex m_mutex;
  std::vector<std::string> m_droppedPaths;
};

//
// MSWindowsDropTarget
//

MSWindowsDropTarget::MSWindowsDropTarget() : m_impl(new Impl())
{
}

MSWindowsDropTarget::~MSWindowsDropTarget()
{
  delete m_impl;
}

bool MSWindowsDropTarget::registerTarget(HWND hwnd)
{
  return m_impl != nullptr && m_impl->registerTarget(hwnd);
}

void MSWindowsDropTarget::unregisterTarget()
{
  if (m_impl != nullptr) {
    m_impl->unregisterTarget();
  }
}

std::vector<std::string> MSWindowsDropTarget::takeDroppedPaths()
{
  if (m_impl == nullptr) {
    return {};
  }
  return m_impl->takeDroppedPaths();
}

} // namespace deskflow::platform
