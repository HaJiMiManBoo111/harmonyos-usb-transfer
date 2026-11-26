/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"

#include <string>
#include <string_view>

class IEventQueue;

class StreamChunker
{
public:
  static void sendClipboard(
      const std::string_view &data, size_t size, ClipboardID id, uint32_t sequence, IEventQueue *events,
      void *eventTarget
  );

  //! Queue one complete file transfer: start header, data chunks, end header.
  /*!
  Every queued message is delivered as EventTypes::FileChunkSending and is
  written to the stream by a FileChunk::send() handler. The payload of a single
  data chunk is capped at s_fileChunkPayloadSize (512 KB) so that the resulting
  DFTR body stays well below PROTOCOL_MAX_STRING_LENGTH.

  A zero sized file still produces the start and end headers; the data loop is
  skipped, which also keeps the sent length accounting from underflowing.
  */
  static void sendFile(
      const std::string &data, size_t size, uint32_t transferId, IEventQueue *events, void *eventTarget
  );
};
