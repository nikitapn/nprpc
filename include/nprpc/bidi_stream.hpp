// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <utility>

#include "stream_reader.hpp"
#include "stream_writer.hpp"

namespace nprpc {

/// Both directions of a `bidi_stream` method: read what the other side
/// sends, write what you send. Move-only.
template <typename TIn, typename TOut>
struct BidiStream {
  /// Incoming items.
  StreamReader<TIn> reader;
  /// Outgoing items.
  StreamWriter<TOut> writer;

  /// Pairs a reader and a writer for one stream.
  BidiStream(StreamReader<TIn>&& in_reader, StreamWriter<TOut>&& out_writer)
      : reader(std::move(in_reader))
      , writer(std::move(out_writer))
  {
  }

  BidiStream(BidiStream&&) noexcept = default;
  BidiStream& operator=(BidiStream&&) = delete;
  BidiStream(const BidiStream&) = delete;
  BidiStream& operator=(const BidiStream&) = delete;
};

} // namespace nprpc