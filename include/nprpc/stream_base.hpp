// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/export.hpp>
#include <nprpc/flat_buffer.hpp>

namespace nprpc {

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
