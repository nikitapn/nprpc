// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <limits>

#include <nprpc/export.hpp>
#include <nprpc/flat_buffer.hpp>

namespace nprpc {

inline constexpr uint64_t kEmptyStreamFinalSequence = std::numeric_limits<uint64_t>::max();

inline constexpr uint64_t stream_final_sequence_for_sent_chunks(uint64_t sent_chunk_count) noexcept
{
  return sent_chunk_count == 0 ? kEmptyStreamFinalSequence : sent_chunk_count - 1;
}

// Base class for type-erased storage of StreamWriters
class NPRPC_API StreamWriterBase
{
public:
  virtual ~StreamWriterBase() = default;
  virtual void resume() = 0;
  virtual bool is_done() const = 0;
  virtual void cancel() = 0;
};

// Base class for type-erased storage of StreamReaders
class NPRPC_API StreamReaderBase
{
public:
  virtual ~StreamReaderBase() = default;
  virtual void on_chunk_received(flat_buffer fb) = 0;
  virtual void on_complete() = 0;
  virtual void on_error(uint32_t error_code, flat_buffer fb) = 0;
};

} // namespace nprpc
