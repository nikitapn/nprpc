// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <string>
#include <filesystem>
#include <optional>
#include <vector>
#include <stdexcept>

namespace npidl {

// Error types needed by interfaces
class lexical_error : public std::runtime_error {
public:
  const std::string file_path;
  const int line;
  const int col;

  lexical_error(std::string_view _file_path, int _line, int _col, const char* msg)
    : std::runtime_error(msg), file_path(_file_path), line(_line), col(_col) {}
  
  lexical_error(std::string_view _file_path, int _line, int _col, const std::string& msg)
    : std::runtime_error(msg), file_path(_file_path), line(_line), col(_col) {}
};

class parser_error : public lexical_error {
public:
  parser_error(std::string_view _file_path, int _line, int _col, const char* msg)
    : lexical_error(_file_path, _line, _col, msg) {}

  parser_error(std::string_view _file_path, int _line, int _col, const std::string& msg)
    : lexical_error(_file_path, _line, _col, msg) {}
};

// Interface: Where does source code come from?
class ISourceProvider {
public:
    virtual ~ISourceProvider() = default;
    virtual std::string read_file(const std::filesystem::path& path) = 0;
};

// Interface: How do we resolve and handle imports?
class IImportResolver {
public:
    virtual ~IImportResolver() = default;
    
    // Resolve an import path relative to current file
    // Returns nullopt if import cannot be resolved
    virtual std::optional<std::filesystem::path> resolve_import(
        const std::string& import_path,
        const std::filesystem::path& current_file_path
    ) = 0;
    
    // Check if we should parse this import file
    // (Used to prevent circular imports and duplicate parsing)
    virtual bool should_parse_import(const std::filesystem::path& resolved_path) = 0;
};

// Interface: How do we handle parsing errors?
class IErrorHandler {
public:
    virtual ~IErrorHandler() = default;
    
    // Handle a parsing error
    virtual void handle_error(const parser_error& error) = 0;
    
    // Should parser continue after error? (LSP=yes, Compiler=no)
    virtual bool should_continue_after_error() const = 0;
};

class ICompilation {
  friend class CompilationBuilder;
  std::unique_ptr<struct Compilation> impl_;
public:
  ~ICompilation();
  void compile();
  const std::vector<parser_error>& get_errors() const;
  bool has_errors() const;
};

class CompilationBuilder {
public:
  CompilationBuilder& set_input_files(const std::vector<std::filesystem::path>& input_files);
  CompilationBuilder& set_output_dir(const std::filesystem::path& output_dir);
  CompilationBuilder& with_language_cpp();
  CompilationBuilder& with_language_ts();
  std::unique_ptr<ICompilation> build();
private:
  enum LanguageFlags {
    None       = 0x00, // LSP mode only
    Cpp        = 0x01, // C++ output
    TypeScript = 0x02, // TypeScript output
  };

  int language_flags_ = None;
  std::filesystem::path output_dir_;
  std::vector<std::filesystem::path> input_files_;
};

} // namespace npidl