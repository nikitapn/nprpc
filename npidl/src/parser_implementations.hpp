// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "parser_interfaces.hpp"
#include <unordered_map>
#include <unordered_set>

namespace npidl {

// ============================================================================
// Concrete ISourceProvider Implementations
// ============================================================================

class FileSystemSourceProvider : public ISourceProvider
{
public:
  std::string read_file(const std::filesystem::path& path) override;
};

class LspSourceProvider : public ISourceProvider
{
  std::unordered_map<std::string, std::string> documents_;

public:
  void update_document(const std::string& uri, const std::string& content);
  void remove_document(const std::string& uri);
  std::string read_file(const std::filesystem::path& path) override;
  bool has_document(const std::string& uri) const;
};

// In-memory source provider for testing - provides content directly without
// file I/O
class InMemorySourceProvider : public ISourceProvider
{
  std::string content_;

public:
  explicit InMemorySourceProvider(std::string content)
      : content_(std::move(content))
  {
  }

  std::string read_file(const std::filesystem::path& path) override
  {
    // Ignore path, just return the in-memory content
    return content_;
  }
};

// ============================================================================
// Concrete IImportResolver Implementations
// ============================================================================

class CompilerImportResolver : public IImportResolver
{
  std::unordered_set<std::string> parsed_files_;

public:
  std::optional<std::filesystem::path>
  resolve_import(const std::string& import_path,
                 const std::filesystem::path& current_file_path) override;

  bool should_parse_import(const std::filesystem::path& resolved_path) override;
};

class LspImportResolver : public IImportResolver
{
public:
  std::optional<std::filesystem::path>
  resolve_import(const std::string& import_path,
                 const std::filesystem::path& current_file_path) override;

  bool should_parse_import(const std::filesystem::path& resolved_path) override;
};

// ============================================================================
// Concrete IErrorHandler Implementations
// ============================================================================

class CompilerErrorHandler : public IErrorHandler
{
public:
  void handle_error(const parser_error& error) override;
  bool should_continue_after_error() const override;
};

class LspErrorHandler : public IErrorHandler
{
  std::vector<parser_error> errors_;

public:
  void handle_error(const parser_error& error) override;
  bool should_continue_after_error() const override;

  const std::vector<parser_error>& get_errors() const;
  void clear_errors();
};

} // namespace npidl
