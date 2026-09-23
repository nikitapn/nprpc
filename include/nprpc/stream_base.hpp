// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <limits>

#include <nprpc/export.hpp>
#include <nprpc/flat_buffer.hpp>

namespace nprpc {

/// `StreamComplete.final_sequence` of a stream that sent no chunks.
inline constexpr uint64_t kEmptyStreamFinalSequence = std::numeric_limits<uint64_t>::max();

/// `StreamComplete.final_sequence` after `sent_chunk_count` chunks.
inline constexpr uint64_t stream_final_sequence_for_sent_chunks(uint64_t sent_chunk_count) noexcept
{
  return sent_chunk_count == 0 ? kEmptyStreamFinalSequence : sent_chunk_count - 1;
}

/// Type-erased `StreamWriter`, as the runtime stores it.
class NPRPC_API StreamWriterBase
{
public:
  virtual ~StreamWriterBase() = default;
  /// Runs the producer until it next waits for credits or finishes.
  virtual void resume() = 0;
  /// Whether the producer has finished.
  virtual bool is_done() const = 0;
  /// Stops the producer, e.g. when the reader cancels.
  virtual void cancel() = 0;
};

/// Type-erased `StreamReader`, as the runtime stores it.
class NPRPC_API StreamReaderBase
{
public:
  virtual ~StreamReaderBase() = default;
  /// A chunk arrived.
  virtual void on_chunk_received(flat_buffer fb) = 0;
  /// The producer finished.
  virtual void on_complete() = 0;
  /// The producer failed with `error_code`; `fb` holds any exception data.
  virtual void on_error(uint32_t error_code, flat_buffer fb) = 0;
};

} // namespace nprpc
