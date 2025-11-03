// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <stdexcept>
#include <memory>

// Forward declarations for Parser integration with LSP

namespace npidl {

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