// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <string>

namespace npidl {

// Forward declarations
struct Token;
class Context;
class ISourceProvider;

// Interface for Lexer - tokenization
class ILexer
{
public:
  virtual ~ILexer() = default;

  // Get next token
  virtual Token tok() = 0;

  // Current position in source
  virtual int line() const noexcept = 0;
  virtual int col() const noexcept = 0;

  // Access to source provider (for creating child lexers in imports)
  virtual ISourceProvider& get_source_provider() = 0;
};

// Interface for Parser - syntax analysis
class IParser
{
public:
  virtual ~IParser() = default;

  // Parse the source code
  virtual void parse() = 0;
};

} // namespace npidl
