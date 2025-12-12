// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <variant>
#include <vector>

#include "source_location.hpp"

namespace npidl {

constexpr int fundamental_type_first = 256;
constexpr int fundamental_type_last = fundamental_type_first + 16;

#define ONE_CHAR_TOKENS()                                                      \
  TOKEN_FUNC(Hash, '#')                                                        \
  TOKEN_FUNC(RoundBracketOpen, '(')                                            \
  TOKEN_FUNC(RoundBracketClose, ')')                                           \
  TOKEN_FUNC(Comma, ',')                                                       \
  TOKEN_FUNC(Semicolon, ';')                                                   \
  TOKEN_FUNC(Assignment, '=')                                                  \
  TOKEN_FUNC(Optional, '?')                                                    \
  TOKEN_FUNC(Less, '<')                                                        \
  TOKEN_FUNC(Greater, '>')                                                     \
  TOKEN_FUNC(SquareBracketOpen, '[')                                           \
  TOKEN_FUNC(SquareBracketClose, ']')                                          \
  TOKEN_FUNC(BracketOpen, '{')                                                 \
  TOKEN_FUNC(BracketClose, '}')                                                \
  TOKEN_FUNC(Dot, '.')

enum class TokenId {
  Unknown = 0,
  Colon = ':',

#define TOKEN_FUNC(x, y) x = y,
  ONE_CHAR_TOKENS()
#undef TOKEN_FUNC

      Boolean = fundamental_type_first,
  Int8,
  UInt8,
  Int16,
  UInt16,
  Int32,
  UInt32,
  Int64,
  UInt64,
  Float32,
  Float64 = fundamental_type_last,

  Identifier,
  Number,
  String,
  Vector,
  Message,
  Exception,
  Eof,
  DoubleColon,
  Namespace,
  Interface,
  Object,
  Void,
  In,
  Out,
  Enum,
  Alias,
  Raises,
  OutDirect,
  Helper,
  Trusted,
  Async,
  Const,
  Module,
  Import,
  QuotedString,
};

struct AstStructDecl;
struct AstTypeDecl;
struct AstInterfaceDecl;
struct AstFunctionDecl;
struct AstNumber;

class Namespace
{
  Namespace* parent_;
  std::string name_;

  std::vector<Namespace*> children_;
  std::vector<std::pair<std::string, AstTypeDecl*>> types_;
  std::vector<std::pair<std::string, AstNumber>> constants_;

  std::string construct_path(std::string delim, int level) const noexcept;

public:
  Namespace()
      : parent_(nullptr)
  {
  }

  Namespace(Namespace* parent, std::string&& name)
      : parent_(parent)
      , name_(std::move(name))
  {
  }

  const std::string& name() const noexcept { return name_; }

  template <typename T> void change_name(T&& new_name) noexcept
  {
    name_ = std::forward<T>(new_name);
  }

  Namespace* parent() const noexcept { return parent_; }

  Namespace* root() noexcept
  {
    auto ptr = this;
    while (ptr->parent())
      ptr = ptr->parent();
    return ptr;
  }

  int length() const noexcept
  {
    int len = 0;
    for (auto ptr = this; ptr; ptr = ptr->parent(), ++len)
      ;
    return len;
  }

  std::string to_cpp17_namespace(int level = -1) const noexcept
  {
    return construct_path(std::string(2, ':'), level);
  }

  std::string to_ts_namespace(int level = -1) const noexcept
  {
    return construct_path(std::string(1, '.'), level);
  }

  static std::pair<Namespace*, int> substract(Namespace* from, Namespace* what);

  std::vector<Namespace*> path() noexcept;
  std::vector<const Namespace*> path() const noexcept;
  Namespace* find_child(const std::string& str);
  AstNumber* find_constant(const std::string& name);
  AstTypeDecl* find_type(const std::string& str, bool only_this_namespace);
  void add(const std::string& name, AstTypeDecl* type);
  void add_constant(std::string&& name, AstNumber&& number);
  Namespace* push(std::string&& s) noexcept;
  void push(Namespace* nm) noexcept
  {
    nm->parent_ = this;
    children_.push_back(nm);
  }

  // Accessors for iteration (needed for LSP indexing)
  const std::vector<std::pair<std::string, AstTypeDecl*>>&
  types() const noexcept
  {
    return types_;
  }
  const std::vector<Namespace*>& children() const noexcept { return children_; }
};

enum class FieldType {
  Fundamental,
  Array,
  Vector,
  String,
  Struct,
  Optional,
  Void,
  Object,
  Interface,
  Alias,
  Enum,
};

enum class NumberFormat { Decimal, Hex, Scientific };

struct AstNumber {
  std::variant<std::int64_t, float, double, bool> value;
  NumberFormat format;
  int max_size = 0;

  std::int64_t decimal() const noexcept
  {
    assert(std::holds_alternative<std::int64_t>(value));
    return std::get<std::int64_t>(value);
  }

  bool is_decimal() const noexcept
  {
    return std::holds_alternative<std::int64_t>(value);
  }
};

inline bool operator==(std::int64_t x, const AstNumber& n)
{
  assert(std::holds_alternative<std::int64_t>(n.value));
  return std::get<std::int64_t>(n.value) == x;
}

inline bool operator!=(std::int64_t x, const AstNumber& n) { return !(x == n); }

// helper type for the visitor #4
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
// template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

inline std::ostream& operator<<(std::ostream& os, const AstNumber& n)
{
  std::visit(overloaded{
                 [&](int64_t x) {
                   if (n.format == NumberFormat::Hex) {
                     os << "0x" << std::hex;
                     if (n.max_size) {
                       os << std::setfill('0')
                          << std::setw(
                                 static_cast<std::streamsize>(n.max_size) * 2);
                     }
                     os << x;
                     os << std::dec;
                   } else {
                     os << x;
                   }
                 },
                 [&](float x) { os << x; },
                 [&](double x) { os << x; },
                 [&](bool x) { os << x; },
             },
             n.value);
  return os;
}

struct AstTypeDecl {
  FieldType id;
};

struct AstObjectDecl : AstTypeDecl {
  AstObjectDecl() { id = FieldType::Object; }
};

struct AstVoidDecl : AstTypeDecl {
  AstVoidDecl() { id = FieldType::Void; }
};

struct AstFundamentalType : AstTypeDecl {
  TokenId token_id;

  AstFundamentalType(TokenId _token_id)
      : token_id(_token_id)
  {
    id = FieldType::Fundamental;
  }
};

struct AstWrapType : AstTypeDecl {
  AstTypeDecl* type;
  AstWrapType(AstTypeDecl* _type)
      : type(_type)
  {
  }

  AstTypeDecl* real_type();
};

struct AstArrayDecl : AstWrapType {
  const int length;

  AstArrayDecl(AstTypeDecl* _type, int _length)
      : AstWrapType(_type)
      , length(_length)
  {
    id = FieldType::Array;
  }
};

struct AstVectorDecl : AstWrapType {
  AstVectorDecl()
      : AstWrapType(nullptr)
  {
    id = FieldType::Vector;
  }
};

struct AstAliasDecl : AstWrapType, AstNodeWithPosition {
  Namespace* nm;

  AstTypeDecl* get_real_type() const noexcept
  {
    auto t = this;
    while (t->type->id == FieldType::Alias)
      t = static_cast<AstAliasDecl*>(t->type);
    return t->type;
  }

  AstAliasDecl(std::string&& _name, Namespace* _nm, AstTypeDecl* _type)
      : AstWrapType(_type)
      , nm(_nm)
  {
    name = std::move(_name);
    id = FieldType::Alias;
  }
};

struct AstStringDecl : AstTypeDecl {
  AstStringDecl() { id = FieldType::String; }
};

struct AstInterfaceDecl : AstTypeDecl, AstNodeWithPosition {
  std::vector<AstFunctionDecl*> fns;
  std::vector<AstInterfaceDecl*> plist;
  bool trusted = true;
  bool is_udp =
      false; // [udp] attribute - all methods are fire-and-forget by default

  AstInterfaceDecl() { id = FieldType::Interface; }
};

struct AstEnumDecl : AstFundamentalType, AstNodeWithPosition {
  Namespace* nm;
  std::vector<std::pair<std::string, std::pair<AstNumber, bool>>> items;

  AstEnumDecl()
      : AstFundamentalType(TokenId::UInt32)
  {
    id = FieldType::Enum;
  }
};

struct AstFieldDecl : AstNodeWithPosition {
  AstTypeDecl* type;
  SourceRange type_ref_range; // Position of the type name in source (for
                              // go-to-definition)
  bool function_argument = false;
  bool input_function_argument;
  std::string_view function_name;
  std::string_view function_argument_name;

  bool is_optional() const noexcept { return type->id == FieldType::Optional; }
};

using struct_id_t = std::string;

struct AstImportDecl {
  std::string import_path; // Path as written in source: "./types.npidl"
  std::filesystem::path resolved_path; // Absolute resolved path
  bool resolved = false;     // True if import was successfully resolved
  std::string error_message; // Error message if resolution failed

  // Position information for LSP
  // Full range: import "./types.npidl";
  //             ^^^^^^^^^^^^^^^^^^^^^^^
  int import_line = 0;
  int import_col = 0;

  // Path range: "./types.npidl"
  //             ^^^^^^^^^^^^^^^
  int path_start_line = 0;
  int path_start_col = 0;
  int path_end_col = 0;
};

struct AstStructDecl : AstTypeDecl, AstNodeWithPosition {
  int version = -1;
  Namespace* nm;
  struct_id_t unique_id;
  std::vector<AstFieldDecl*> fields;
  int size = -1;
  int align = -1;
  bool flat = true;
  bool has_span_class = false;
  int exception_id;

  bool is_exception() const noexcept { return exception_id != -1; }
  const struct_id_t& get_function_struct_id();

  AstStructDecl() { id = FieldType::Struct; }
};

struct AstOptionalDecl : AstWrapType {
  AstOptionalDecl(AstTypeDecl* _type)
      : AstWrapType(_type)
  {
    id = FieldType::Optional;
  }
};

constexpr auto cft(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Fundamental || type->id == FieldType::Enum);
  return static_cast<AstFundamentalType*>(type);
}

constexpr auto cft(const AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Fundamental || type->id == FieldType::Enum);
  return static_cast<const AstFundamentalType*>(type);
}

constexpr auto cwt(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Array || type->id == FieldType::Vector ||
         type->id == FieldType::Alias || type->id == FieldType::Optional);
  return static_cast<AstWrapType*>(type);
}

constexpr auto car(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Array);
  return static_cast<AstArrayDecl*>(type);
}

constexpr auto cvec(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Vector);
  return static_cast<AstVectorDecl*>(type);
}

constexpr auto cstr(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::String);
  return static_cast<AstStringDecl*>(type);
}

constexpr auto cobj(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Object);
  return static_cast<AstObjectDecl*>(type);
}

constexpr auto cflat(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Struct);
  return static_cast<AstStructDecl*>(type);
}

constexpr auto cenum(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Enum);
  return static_cast<AstEnumDecl*>(type);
}

constexpr auto cenum(const AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Enum);
  return static_cast<const AstEnumDecl*>(type);
}

constexpr auto calias(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Alias);
  return static_cast<AstAliasDecl*>(type);
}

constexpr auto calias(const AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Alias);
  return static_cast<const AstAliasDecl*>(type);
}

constexpr auto cifs(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Interface);
  return static_cast<AstInterfaceDecl*>(type);
}

constexpr auto copt(AstTypeDecl* type) noexcept
{
  assert(type->id == FieldType::Optional);
  return static_cast<AstOptionalDecl*>(type);
}

inline AstTypeDecl* AstWrapType::real_type()
{
  auto wt = type;
  if (wt->id == FieldType::Alias)
    wt = calias(wt)->get_real_type();
  return wt;
}

enum class ArgumentModifier { In, Out };

struct AstFunctionArgument : AstFieldDecl {
  ArgumentModifier modifier;
  bool direct = false;
};

struct AstFunctionDecl : AstNodeWithPosition {
  uint16_t idx;
  AstTypeDecl* ret_value;
  AstStructDecl* in_s = nullptr;
  AstStructDecl* out_s = nullptr;
  bool arguments_structs_have_been_made = false;
  AstStructDecl* ex = nullptr;
  std::vector<AstFunctionArgument*> args, in_args, out_args;
  bool is_async;
  bool is_reliable =
      true; // [unreliable] attribute maybe used for UDP and quic methods

  bool is_void() const noexcept { return ret_value->id == FieldType::Void; }
};

template <typename IdType, typename T> class List
{
  static_assert(std::is_pointer_v<T>);
  std::vector<std::tuple<IdType, T>> items_;

public:
  T get(const IdType& id)
  {
    if (auto it = std::find_if(items_.begin(), items_.end(),
                               [id](auto& t) { return std::get<0>(t) == id; });
        it != items_.end()) {
      return std::get<1>(*it);
    }
    return nullptr;
  }
  void put(const IdType& id, T item) { items_.emplace_back(id, item); }
  auto begin() { return items_.begin(); }
  auto end() { return items_.end(); }
};

using AFFAList = List<struct_id_t, AstStructDecl*>;

class Context
{
  Namespace* nm_root_;
  Namespace* nm_cur_;
  int exception_id_last = -1;

  std::string module_name;
  int module_level = 0;
  std::unordered_set<AstStructDecl*> structs_with_helpers_;

  // File stack for tracking imports
  // Stack always contains at least one element (the main/current file)
  // When parsing imports, we push the imported file onto stack
  struct FileContext {
    std::filesystem::path file_path;
    std::string base_name;
    Namespace* namespace_at_entry; // Namespace state when file started parsing
  };
  std::vector<FileContext> file_stack_;

public:
  AFFAList affa_list;
  int m_struct_n_ = 0;
  std::vector<AstStructDecl*> exceptions;
  std::vector<AstInterfaceDecl*> interfaces;
  std::vector<AstImportDecl*> imports; // All imports in this file

  const std::string& current_file() const noexcept
  {
    assert(!file_stack_.empty());
    return file_stack_.back().base_name;
  }

  const std::filesystem::path& get_file_path() const noexcept
  {
    assert(!file_stack_.empty());
    return file_stack_.back().file_path;
  }

  std::string current_file_path() const noexcept
  {
    assert(!file_stack_.empty());
    const auto& file_path = file_stack_.back().file_path;
    // Use canonical path if file exists, otherwise use absolute path
    // This is safe for LSP mode where file might not exist on disk yet
    std::error_code ec;
    auto canonical = std::filesystem::canonical(file_path, ec);
    if (!ec) {
      return canonical.string();
    }
    return std::filesystem::absolute(file_path).string();
  }

  const std::string& module() const noexcept { return module_name; }

  void set_module_name(std::vector<std::string>&& name_parts);
  int get_module_level() const noexcept { return module_level; }

  int next_exception_id() noexcept { return ++exception_id_last; }

  auto push_namespace(std::string&& s)
  {
    nm_cur_ = nm_cur_->push(std::move(s));
    return nm_cur_;
  }

  void pop_namespace()
  {
    nm_cur_ = nm_cur_->parent();
    assert(nm_cur_);
  }

  Namespace* nm_cur() { return nm_cur_; }

  Namespace* nm_root() { return nm_root_; }

  Namespace* set_namespace(Namespace* nm)
  {
    auto old = nm_cur_;
    nm_cur_ = nm;
    return old;
  }

  auto get_structs_with_helpers() const { return structs_with_helpers_; }

  void mark_struct_as_having_helpers(AstStructDecl* s)
  {
    structs_with_helpers_.insert(s);
  }

  bool is_nprpc_base() const noexcept
  {
    assert(!file_stack_.empty());
    return file_stack_.back().base_name == "nprpc_base";
  }

  bool is_nprpc_nameserver() const noexcept
  {
    assert(!file_stack_.empty());
    return file_stack_.back().base_name == "nprpc_nameserver";
  }

  // Push new file onto stack (for imports)
  void push_file(std::filesystem::path new_file_path)
  {
    std::string new_base_name =
        new_file_path.filename().replace_extension().string();
    std::transform(new_base_name.begin(), new_base_name.end(),
                   new_base_name.begin(),
                   [](char c) { return c == '.' ? '_' : ::tolower(c); });

    file_stack_.push_back(FileContext{
        .file_path = std::move(new_file_path),
        .base_name = std::move(new_base_name),
        .namespace_at_entry = nm_cur_ // Save namespace for restoration
    });
  }

  // Pop back to previous file after import completes
  void pop_file()
  {
    if (file_stack_.size() <= 1) {
      throw std::runtime_error(
          "Cannot pop file: only main file remains in stack");
    }

    // Restore namespace from the file being popped
    nm_cur_ = file_stack_.back().namespace_at_entry;
    file_stack_.pop_back();
  }

  // Check if we're currently parsing an imported file
  bool is_in_import() const noexcept
  {
    return file_stack_.size() > 1; // More than just main file
  }

  // Get the depth of import nesting (0 = main file, 1 = first import, etc.)
  size_t import_depth() const noexcept
  {
    return file_stack_.size() > 0 ? file_stack_.size() - 1 : 0;
  }

  // Get path of the file that imported current file (if any)
  std::optional<std::filesystem::path> parent_file_path() const noexcept
  {
    if (file_stack_.size() < 2)
      return std::nullopt;
    return file_stack_[file_stack_.size() - 2].file_path;
  }

  Context(std::filesystem::path initial_file_path = "<in-memory>")
      : nm_root_(new Namespace())
      , nm_cur_(nm_root_)
  {
    // Initialize file stack with the main file
    std::string base_name =
        initial_file_path.filename().replace_extension().string();
    std::transform(base_name.begin(), base_name.end(), base_name.begin(),
                   [](char c) { return c == '.' ? '_' : ::tolower(c); });

    file_stack_.push_back(FileContext{
        .file_path = std::move(initial_file_path),
        .base_name = std::move(base_name),
        .namespace_at_entry = nm_root_ // Main file starts at root namespace
    });
  }
};

// RAII guard for file context - ensures pop_file() is called even on exception
class FileContextGuard
{
  Context& ctx_;
  bool dismissed_ = false;

public:
  explicit FileContextGuard(Context& ctx, std::filesystem::path file_path)
      : ctx_(ctx)
  {
    ctx_.push_file(std::move(file_path));
  }

  ~FileContextGuard()
  {
    if (!dismissed_) {
      try {
        ctx_.pop_file();
      } catch (...) {
        // Suppress exceptions in destructor
      }
    }
  }

  // Call this if you want to handle pop manually
  void dismiss() noexcept { dismissed_ = true; }

  // Delete copy/move to prevent misuse
  FileContextGuard(const FileContextGuard&) = delete;
  FileContextGuard& operator=(const FileContextGuard&) = delete;
  FileContextGuard(FileContextGuard&&) = delete;
  FileContextGuard& operator=(FileContextGuard&&) = delete;
};

} // namespace npidl