/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_5.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/FileChunk.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <cstring>
#include <string>

//
// ClientProxy1_5
//

ClientProxy1_5::ClientProxy1_5(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_4(name, stream, server, events),
      m_fileEvents(events)
{
  // Queued by StreamChunker::sendFile(); each event owns one FileChunk that is
  // written here and then released by the event queue.
  if (m_fileEvents != nullptr) {
    m_fileEvents->addHandler(EventTypes::FileChunkSending, this, [this](const auto &event) {
      FileChunk::send(getStream(), event.getDataObject());
    });
  }
}

ClientProxy1_5::~ClientProxy1_5()
{
  if (m_fileEvents != nullptr) {
    m_fileEvents->removeHandler(EventTypes::FileChunkSending, this);
  }
}

void ClientProxy1_5::sendDragInfo(uint32_t fileCount, const char *info, size_t size)
{
  if (info == nullptr && size != 0) {
    LOG_ERR("cannot send drag info, missing path list");
    return;
  }

  // DDRG%2i%s: the count is a 2 byte big endian integer and the path list is a
  // NUL separated, length prefixed string (ProtocolUtil.cpp:59-66, :32-37).
  std::string paths(info != nullptr ? std::string(info, size) : std::string());
  ProtocolUtil::writef(getStream(), kMsgDDragInfo, static_cast<uint32_t>(fileCount), &paths);

  LOG_DEBUG("sent drag info, files=%u bytes=%zu", fileCount, size);
}

void ClientProxy1_5::fileChunkSending(uint8_t mark, char *data, size_t dataSize)
{
  if (data == nullptr && dataSize != 0) {
    LOG_ERR("cannot send file chunk, missing payload");
    return;
  }

  // The mark is the outer field of "DFTR%1i%s"; everything else (transferId,
  // chunk type and payload) already lives inside the length prefixed body that
  // FileChunk produced.
  const std::string body(data != nullptr ? std::string(data, dataSize) : std::string());
  ProtocolUtil::writef(getStream(), kMsgDFileTransfer, static_cast<uint32_t>(mark), &body);
}

bool ClientProxy1_5::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDFileTransfer, 4) == 0) {
    fileChunkReceived();
  } else if (memcmp(code, kMsgDDragInfo, 4) == 0) {
    dragInfoReceived();
  } else {
    return ClientProxy1_4::parseMessage(code);
  }

  return true;
}

void ClientProxy1_5::fileChunkReceived()
{
  FileChunkMessage message;
  if (!FileChunk::assemble(getStream(), message)) {
    LOG_ERR("failed to read file chunk from client");
    return;
  }

  LOG_DEBUG(
      "received file chunk, id=%u type=%u size=%zu", message.m_transferId, message.m_chunkType,
      message.m_payload.size()
  );

  if (m_fileEvents == nullptr) {
    return;
  }

  m_fileEvents->addEvent(
      Event(EventTypes::FileChunkReceived, getEventTarget(), new FileChunkMessage(std::move(message)))
  );
}

void ClientProxy1_5::dragInfoReceived()
{
  if (getStream() == nullptr) {
    return;
  }

  uint16_t fileCount = 0;
  std::string info;
  if (!ProtocolUtil::readf(getStream(), kMsgDDragInfo + 4, &fileCount, &info)) {
    LOG_ERR("failed to read drag info from client");
    return;
  }

  auto *dragInfo = new FileDragInfo();
  size_t start = 0;
  while (start < info.size()) {
    const size_t end = info.find('\0', start);
    const size_t length = (end == std::string::npos ? info.size() : end) - start;
    if (length > 0) {
      dragInfo->m_paths.push_back(info.substr(start, length));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  LOG_DEBUG("received drag info, files=%u paths=%zu", fileCount, dragInfo->m_paths.size());

  if (m_fileEvents == nullptr) {
    delete dragInfo;
    return;
  }

  m_fileEvents->addEvent(Event(EventTypes::FileDragInfoReceived, getEventTarget(), dragInfo));
}
