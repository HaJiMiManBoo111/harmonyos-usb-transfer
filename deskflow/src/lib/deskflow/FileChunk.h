/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/Chunk.h"
#include "deskflow/ProtocolTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

//! Inner metadata of a DFTR payload: transfer id plus chunk type.
/*!
 * The wire format of a file transfer message is produced by the protocol
 * formatter and is not assembled by hand:
 *
 *   "DFTR" (4) | mark (1) | length (4, big endian) | data (length bytes)
 *
 * `mark` is the outer ChunkType and `data` is what this class serialises:
 *
 *   transferId (4, little endian) | chunkType (1) | payload
 *
 * The outer 4 byte length prefix is written by ProtocolUtil::writeString()
 * (ProtocolUtil.cpp:59-66) which calls writeInt() with a length of 4;
 * writeInt() emits big endian for that width (ProtocolUtil.cpp:32-37).
 * That outer prefix therefore stays big endian.
 *
 * The inner `data` bytes -- the 4 byte transfer id and the 8 byte file size
 * header -- are little endian, which is what the HarmonyOS client expects
 * (deskflow_client.cpp:237-251 reads them with readLE32/readLE64).
 * FileChunk therefore only ever produces the inner `data` bytes.
 */
constexpr static auto s_fileChunkMetaSize = 5; // transferId (4) + chunkType (1)

//! Size of the file size header carried inside a DataStart payload.
constexpr static auto s_fileChunkSizeHeaderSize = 8;

//! Largest payload carried by a single DFTR message.
/*!
 * A DFTR body goes through the %s format which is limited to
 * PROTOCOL_MAX_STRING_LENGTH (ProtocolTypes.h:140) == 1 MB; exceeding it makes
 * ProtocolUtil::readf() throw BadClientException (ProtocolUtil.cpp:191-201)
 * and tears down the whole connection. 512 KB leaves room for the metadata.
 */
constexpr static auto s_fileChunkPayloadSize = 512 * 1024;

//! Upper bound accepted for a declared file size, purely a sanity guard.
constexpr static auto s_fileChunkMaxFileSize = 100ULL * 1024 * 1024 * 1024;

namespace deskflow {
class IStream;
}

//! One decoded file transfer message, without the outer mark.
class FileChunkMessage : public EventData
{
public:
  uint32_t m_transferId = 0;
  uint32_t m_chunkType = 0;
  std::string m_payload;
};

//! Per transfer bookkeeping used by the receiving side.
struct FileChunkAssemblyState
{
  uint64_t expectedSize = 0;
  uint64_t receivedSize = 0;
  bool active = false;
};

//! Decoded DDRG file list carried by an event.
class FileDragInfo : public EventData
{
public:
  std::vector<std::string> m_paths;
};

//! Result of one finished or failed transfer, carried by an event.
class FileTransferInfo : public EventData
{
public:
  uint32_t m_transferId = 0;
  std::string m_path;
  uint64_t m_size = 0;
};

class FileChunk : public Chunk
{
public:
  explicit FileChunk(size_t size);

  //! Build the DataStart payload (transfer id, mark, 8 byte little endian size).
  static FileChunk *start(uint32_t transferId, uint64_t fileSize);
  //! Build the DataChunk payload (transfer id, mark, raw file bytes).
  static FileChunk *data(uint32_t transferId, const std::string &data);
  //! Build the DataEnd payload (transfer id, mark).
  static FileChunk *end(uint32_t transferId);

  //! Read and decode one DFTR message from \p stream.
  /*!
  Returns false when the message is truncated or malformed; the caller is
  expected to treat that as a protocol error.
  */
  static bool assemble(deskflow::IStream *stream, FileChunkMessage &message);

  //! Write one queued FileChunk to \p stream.
  static void send(deskflow::IStream *stream, void *data);

  //! Encode a file size into the 8 byte little endian DataStart header.
  static std::string encodeSize(uint64_t size);
  //! Decode the 8 byte little endian DataStart header, 0 when malformed.
  static uint64_t decodeSize(const std::string &payload);

  static size_t getExpectedSize(const FileChunkAssemblyState &state)
  {
    return static_cast<size_t>(state.expectedSize);
  }
};
