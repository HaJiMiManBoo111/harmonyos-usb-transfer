/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/StreamChunker.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/FileChunk.h"

#include <algorithm>

static const size_t g_chunkSize = 512 * 1024; // 512kb

void StreamChunker::sendClipboard(
    const std::string_view &data, size_t size, ClipboardID id, uint32_t sequence, IEventQueue *events, void *eventTarget
)
{
  // send first message (data size)
  std::string dataSize = QString::number(size).toStdString();
  ClipboardChunk *sizeMessage = ClipboardChunk::start(id, sequence, dataSize);

  events->addEvent(Event(EventTypes::ClipboardSending, eventTarget, sizeMessage));

  // send clipboard chunk with a fixed size
  size_t sentLength = 0;
  size_t chunkSize = g_chunkSize;

  while (true) {
    // make sure we don't read too much from the mock data.
    if (sentLength + chunkSize > size) {
      chunkSize = size - sentLength;
    }

    std::string chunk(data.substr(sentLength, chunkSize).data(), chunkSize);
    ClipboardChunk *dataChunk = ClipboardChunk::data(id, sequence, chunk);

    events->addEvent(Event(EventTypes::ClipboardSending, eventTarget, dataChunk));

    sentLength += chunkSize;
    if (sentLength == size) {
      break;
    }
  }

  // send last message
  ClipboardChunk *end = ClipboardChunk::end(id, sequence);

  events->addEvent(Event(EventTypes::ClipboardSending, eventTarget, end));

  LOG_DEBUG("sent clipboard size=%d", sentLength);
}

void StreamChunker::sendFile(
    const std::string &data, size_t size, uint32_t transferId, IEventQueue *events, void *eventTarget
)
{
  if (events == nullptr) {
    LOG_ERR("cannot send file transfer, no event queue");
    return;
  }

  // A DFTR body is carried by the %s format, which the reader rejects above
  // PROTOCOL_MAX_STRING_LENGTH (ProtocolUtil.cpp:191-201); exceeding it kills
  // the whole connection, so the payload stays at the documented chunk size.
  const size_t chunkLimit = std::min<size_t>(g_chunkSize, static_cast<size_t>(s_fileChunkPayloadSize));
  const size_t total = std::min<size_t>(size, data.size());

  // send first message (declared file size)
  events->addEvent(Event(EventTypes::FileChunkSending, eventTarget, FileChunk::start(transferId, total)));

  // send the file content; a zero sized file skips straight to the end header
  size_t sentLength = 0;
  while (sentLength < total) {
    const size_t chunkSize = std::min<size_t>(chunkLimit, total - sentLength);
    const std::string chunk = data.substr(sentLength, chunkSize);

    events->addEvent(Event(EventTypes::FileChunkSending, eventTarget, FileChunk::data(transferId, chunk)));

    sentLength += chunkSize;
  }

  // send last message
  events->addEvent(Event(EventTypes::FileChunkSending, eventTarget, FileChunk::end(transferId)));

  LOG_DEBUG("sent file transfer id=%u size=%zu", transferId, sentLength);
}
