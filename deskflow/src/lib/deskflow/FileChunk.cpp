/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileChunk.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <cstring>
#include <limits>

namespace {

// The inner DFTR payload (transfer id and the 8 byte size header) is little
// endian, matching the HarmonyOS client (deskflow_client.cpp:237-251 uses
// readLE32/readLE64). The outer length prefix stays big endian because it is
// produced by ProtocolUtil::writeInt() (ProtocolUtil.cpp:26-46).
void writeLE32(char *out, uint32_t value)
{
  out[0] = static_cast<char>(value & 0xffU);
  out[1] = static_cast<char>((value >> 8U) & 0xffU);
  out[2] = static_cast<char>((value >> 16U) & 0xffU);
  out[3] = static_cast<char>((value >> 24U) & 0xffU);
}

uint32_t readLE32(const char *in)
{
  return static_cast<uint32_t>(static_cast<uint8_t>(in[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(in[1])) << 8U) |
         (static_cast<uint32_t>(static_cast<uint8_t>(in[2])) << 16U) |
         (static_cast<uint32_t>(static_cast<uint8_t>(in[3])) << 24U);
}

} // namespace

FileChunk::FileChunk(size_t size) : Chunk(size)
{
  m_dataSize = size - s_fileChunkMetaSize;
}

std::string FileChunk::encodeSize(uint64_t size)
{
  std::string encoded(s_fileChunkSizeHeaderSize, '\0');
  for (size_t i = 0; i < s_fileChunkSizeHeaderSize; ++i) {
    const auto shift = static_cast<uint32_t>(i * 8);
    encoded[i] = static_cast<char>((size >> shift) & 0xffULL);
  }
  return encoded;
}

uint64_t FileChunk::decodeSize(const std::string &payload)
{
  if (payload.size() < s_fileChunkSizeHeaderSize) {
    return 0;
  }

  uint64_t size = 0;
  for (size_t i = 0; i < s_fileChunkSizeHeaderSize; ++i) {
    size |= static_cast<uint64_t>(static_cast<uint8_t>(payload[i])) << (i * 8);
  }
  return size;
}

FileChunk *FileChunk::start(uint32_t transferId, uint64_t fileSize)
{
  const auto sizeHeader = encodeSize(fileSize);
  auto *chunk = new FileChunk(s_fileChunkMetaSize + s_fileChunkSizeHeaderSize);
  char *data = chunk->m_chunk;

  writeLE32(data, transferId);
  data[s_fileChunkMetaSize - 1] = static_cast<char>(ChunkType::DataStart);
  std::memcpy(&data[s_fileChunkMetaSize], sizeHeader.data(), s_fileChunkSizeHeaderSize);

  return chunk;
}

FileChunk *FileChunk::data(uint32_t transferId, const std::string &payload)
{
  auto *chunk = new FileChunk(s_fileChunkMetaSize + payload.size());
  char *data = chunk->m_chunk;

  writeLE32(data, transferId);
  data[s_fileChunkMetaSize - 1] = static_cast<char>(ChunkType::DataChunk);
  if (!payload.empty()) {
    std::memcpy(&data[s_fileChunkMetaSize], payload.data(), payload.size());
  }

  return chunk;
}

FileChunk *FileChunk::end(uint32_t transferId)
{
  auto *chunk = new FileChunk(s_fileChunkMetaSize);
  char *data = chunk->m_chunk;

  writeLE32(data, transferId);
  data[s_fileChunkMetaSize - 1] = static_cast<char>(ChunkType::DataEnd);

  return chunk;
}

bool FileChunk::assemble(deskflow::IStream *stream, FileChunkMessage &message)
{
  if (stream == nullptr) {
    return false;
  }

  // kMsgDFileTransfer is "DFTR%1i%s"; skipping the 4 byte code leaves the
  // mark and the length prefixed payload. ProtocolUtil::readf() reads the
  // length as a 4 byte big endian integer and rejects anything above
  // PROTOCOL_MAX_STRING_LENGTH (ProtocolUtil.cpp:191-201).
  uint8_t mark = 0;
  std::string inner;
  if (!ProtocolUtil::readf(stream, kMsgDFileTransfer + 4, &mark, &inner)) {
    LOG_ERR("failed to read file transfer message");
    return false;
  }

  if (inner.size() < s_fileChunkMetaSize) {
    LOG_ERR("file transfer message too short: %zu bytes", inner.size());
    return false;
  }

  message.m_transferId = readLE32(inner.data());
  message.m_chunkType = static_cast<uint8_t>(inner[s_fileChunkMetaSize - 1]);
  message.m_payload = inner.substr(s_fileChunkMetaSize);

  if (message.m_chunkType != mark) {
    LOG_WARN(
        "file transfer chunk type mismatch, outer=%d inner=%d", static_cast<int>(mark),
        static_cast<int>(message.m_chunkType)
    );
  }

  return true;
}

void FileChunk::send(deskflow::IStream *stream, void *data)
{
  auto *fileChunk = static_cast<FileChunk *>(data);
  if (fileChunk == nullptr || stream == nullptr || fileChunk->m_chunk == nullptr) {
    return;
  }

  const char *chunk = fileChunk->m_chunk;
  const auto mark = static_cast<uint8_t>(chunk[s_fileChunkMetaSize - 1]);
  const std::string inner(chunk, s_fileChunkMetaSize + fileChunk->m_dataSize);

  LOG_VERBOSE("sending file transfer chunk, mark=%d size=%zu", static_cast<int>(mark), inner.size());

  // The outer mark and the length prefixed payload are produced by the
  // protocol formatter; this class never writes flat bytes into the stream.
  ProtocolUtil::writef(stream, kMsgDFileTransfer, mark, &inner);
}
