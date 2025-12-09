// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "parser_implementations.hpp"
#include "ast.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace npidl {

// ============================================================================
// FileSystemSourceProvider Implementation
// ============================================================================

std::string
FileSystemSourceProvider::read_file(const std::filesystem::path& path)
{
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("Cannot read file: " + path.string());
  }

  std::ifstream ifs(path);
  std::noskipws(ifs);

  std::string content;
  std::copy(std::istream_iterator<char>(ifs), std::istream_iterator<char>(),
            std::back_inserter(content));

  return content;
}

// ============================================================================
// LspSourceProvider Implementation
// ============================================================================

void LspSourceProvider::update_document(const std::string& uri,
                                        const std::string& content)
{
  documents_[uri] = content;
}

void LspSourceProvider::remove_document(const std::string& uri)
{
  documents_.erase(uri);
}

std::string LspSourceProvider::read_file(const std::filesystem::path& path)
{
  // Convert path to URI for lookup
  std::string uri = "file://" + path.string();

  // Check if we have this document in memory
  if (auto it = documents_.find(uri); it != documents_.end()) {
    return it->second;
  }

  // Fallback to filesystem for imported files not open in editor
  FileSystemSourceProvider fs_provider;
  return fs_provider.read_file(path);
}

bool LspSourceProvider::has_document(const std::string& uri) const
{
  return documents_.find(uri) != documents_.end();
}

// ============================================================================
// CompilerImportResolver Implementation
// ============================================================================

std::optional<std::filesystem::path> CompilerImportResolver::resolve_import(
    const std::string& import_path,
    const std::filesystem::path& current_file_path)
{
  namespace fs = std::filesystem;

  // Resolve relative to current file's directory
  auto base_dir = current_file_path.parent_path();
  auto resolved = fs::absolute(base_dir / import_path);

  // Check if file exists on filesystem
  if (!fs::exists(resolved)) {
    return std::nullopt; // Error: file not found
  }

  return resolved;
}

bool CompilerImportResolver::should_parse_import(
    const std::filesystem::path& resolved_path)
{
  // Convert to canonical path to handle symlinks and relative paths
  std::error_code ec;
  auto canonical = std::filesystem::canonical(resolved_path, ec);
  std::string key = ec ? resolved_path.string() : canonical.string();

  // Parse once per file (avoid circular imports and duplicates)
  return parsed_files_.insert(key).second;
}

// ============================================================================
// LspImportResolver Implementation
// ============================================================================

std::optional<std::filesystem::path> LspImportResolver::resolve_import(
    const std::string& import_path,
    const std::filesystem::path& current_file_path)
{
  namespace fs = std::filesystem;

  // Same resolution logic as compiler
  auto base_dir = current_file_path.parent_path();
  auto resolved = fs::absolute(base_dir / import_path);

  // Don't check if file exists - LSP might have it in memory
  // The source provider will handle the lookup
  return resolved;
}

bool LspImportResolver::should_parse_import(
    const std::filesystem::path& resolved_path)
{
  // Always return true - LSP handles caching at DocumentManager level
  // This keeps Parser stateless
  return true;
}

// ============================================================================
// CompilerErrorHandler Implementation
// ============================================================================

void CompilerErrorHandler::handle_error(const parser_error& error)
{
  // Throw immediately - stop compilation on first error
  throw error;
}

bool CompilerErrorHandler::should_continue_after_error() const { return false; }

// ============================================================================
// LspErrorHandler Implementation
// ============================================================================

void LspErrorHandler::handle_error(const parser_error& error)
{
  // Collect errors for later reporting to LSP client
  errors_.push_back(error);
}

bool LspErrorHandler::should_continue_after_error() const
{
  return true; // Continue parsing to find all errors
}

const std::vector<parser_error>& LspErrorHandler::get_errors() const
{
  return errors_;
}

void LspErrorHandler::clear_errors() { errors_.clear(); }

} // namespace npidl
