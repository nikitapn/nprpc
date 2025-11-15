#include <cstdint>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <optional>
#include <algorithm>
#include <exception>
#include <cassert>
#include <utility>
#include <type_traits>
#include <functional>
#include <charconv>
#include <variant>

#include <boost/container/small_vector.hpp>

#include "ast.hpp"
#include "cpp_builder.hpp"
#include "ts_builder.hpp"
#include "null_builder.hpp"
#include "utils.hpp"
#include "lsp_server.hpp"
#include "parser_interfaces.hpp"
#include "parser_implementations.hpp"
#include "lexer_parser_interfaces.hpp"
#include "parser_factory.hpp"

#include <nprpc/impl/misc/colored_cout.h>

// LSP Helper Function Implementation
#include "parse_for_lsp.hpp"
#include "parser_interfaces.hpp"

namespace npidl {

struct Token {
  TokenId id;
  std::string name;
  std::string_view static_name;
  int line;
  int col;

  Token()
    : id(TokenId::Unknown)
    , line(0)
    , col(0)
  {
  }

  Token(TokenId _id, int _line = 0, int _col = 0)
    : id(_id)
    , line(_line)
    , col(_col)
  {
  }

  Token(TokenId _id, std::string_view _name, int _line = 0, int _col = 0)
    : id(_id)
    , name(_name)
    , line(_line)
    , col(_col)
  {
  }

  Token(TokenId _id, std::string_view _name, std::string_view _static_name, int _line = 0, int _col = 0)
    : id(_id)
    , name(_name)
    , static_name(_static_name)
    , line(_line)
    , col(_col)
  {
  }

  bool operator == (TokenId id) const noexcept {
    return this->id == id;
  }

  bool operator == (char id) const noexcept {
    return this->id == static_cast<TokenId>(id);
  }

    bool operator != (TokenId id) const noexcept {
    return !(operator==(id));
  }

    bool operator != (char id) const noexcept {
    return !(operator==(id));
  }

  bool is_fundamental_type() const noexcept {
    return static_cast<int>(id) >= fundamental_type_first && 
      static_cast<int>(id) <= fundamental_type_last;
  }

  std::string_view to_string_view() const noexcept {
      if (id == TokenId::Identifier || id == TokenId::Number)
        return std::string_view(name);
      if (static_cast<int>(id) < fundamental_type_first) {
        return std::string_view((char*)&id, 1);
      } else {
        return static_name;
    }
  }
};

std::ostream& operator<<(std::ostream& os, const Token& t) {
  os << "id: " << static_cast<int>(t.id) << " n: " << t.to_string_view();
  return os;
}

template<typename Key, typename T, size_t Size>
struct Map {
  const std::pair<Key, T>(&items_)[Size];

  constexpr std::optional<std::pair<Key, T>> find(const Key& key) const noexcept {
    const auto it = std::find_if(items_, items_ + Size,
      [&key](const auto& x) { return x.first == key; });

    if (it != (items_ + Size)) {
      return *it;
    } else {
      return {};
    }
  }
};

class Lexer : public ILexer {
  static constexpr char quote_char = '\'';
  ISourceProvider& source_provider_;
  Context& ctx_;
  std::string text_;
  const char* ptr_;
  int line_ = 1;
  int col_ = 1;

  static constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
  }
  static constexpr bool is_letter_or_underscore(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  }
  static constexpr bool is_valid_name(char c) noexcept {
    return is_digit(c) || is_letter_or_underscore(c) || c == '_';
  }

  static constexpr bool is_delimeter(char c) noexcept {
    switch (c) {
    case ' ': case '\0': case '\n': case '\t':
    case '{': case '}': case '<': case '>': case '[': case ']': case '(': case ')':
    case ':': case ',': case ';': case '/': case '=': case '?': case '.':
      return true;
    default:
      return false;
    }
  }

  void skip_wp() noexcept {
    while (*ptr_ == ' ' || *ptr_ == '\n' || *ptr_ == '\t') next();
  }

  void skip_line_comment() {
    while (*ptr_ != '\n' && *ptr_ != '\0') next();
  }

  void skip_comments() {
    while (*ptr_ != '\0' && !(*ptr_ == '*' && *(ptr_ + 1) == '/')) next();
    if (*ptr_ != '\0') {
      ptr_ += 2;
      col_ += 2;
    }
  }

  char cur() const noexcept { return *ptr_; }

  void next() noexcept {
    if (*ptr_ == '\t') {
      col_ += 2;
    } else if (*ptr_ == '\n') {
      col_ = 1;
      ++line_;
    } else {
      ++col_;
    }
    ++ptr_;
  }

  char look() const noexcept {
    return *(ptr_ + 1);
  }

  std::string read_number() {
    const char* begin = ptr_;

    bool hex = false;

    while (!is_delimeter(cur())) {
      if (cur() == 'x') {
        if (hex) throw lexical_error(ctx_.current_file_path(), line_, col_, "Incorrect hexadecimal number.");
        hex = true;
      } else if (hex) {
        auto c = std::tolower(cur());
        if (!(is_digit(c) || (c >= 'a' && c <= 'f'))) {
          throw lexical_error(ctx_.current_file_path(), line_, col_, "Error.");
        }
      }
      else if (!is_digit(cur())) {
        throw lexical_error(ctx_.current_file_path(), line_, col_, "Error.");
      }
      next();
    }
    return std::string(begin, ptr_ - begin);
  }

  Token read_string(int tok_line, int tok_col) {
    assert(is_letter_or_underscore(cur()));

    const char* begin = ptr_;

    while (!is_delimeter(cur())) {
      if (!is_valid_name(cur())) throw lexical_error(ctx_.current_file_path(), line_, col_, "Error.");
      next();
    }

    using namespace std::string_view_literals;

    static constexpr std::pair<std::string_view, TokenId> alphabet[] =
    {
      { "boolean"sv, TokenId::Boolean },
      { "i8"sv, TokenId::Int8 },
      { "u8"sv, TokenId::UInt8 },
      { "i16"sv, TokenId::Int16 },
      { "u16"sv, TokenId::UInt16 },
      { "i32"sv, TokenId::Int32 },
      { "u32"sv, TokenId::UInt32 },
      { "i64"sv, TokenId::Int64 },
      { "u64"sv, TokenId::UInt64 },
      { "f32"sv, TokenId::Float32 },
      { "f64"sv, TokenId::Float64},
      { "vector"sv, TokenId::Vector},
      { "string"sv, TokenId::String},
      { "message"sv, TokenId::Message},
      { "namespace"sv, TokenId::Namespace},
      { "interface"sv, TokenId::Interface},
      { "object"sv, TokenId::Object},
      { "void"sv, TokenId::Void},
      { "in"sv, TokenId::In },
      { "out"sv, TokenId::Out },
      { "enum"sv, TokenId::Enum },
      { "alias"sv, TokenId::Alias },
      { "exception"sv, TokenId::Exception },
      { "raises"sv, TokenId::Raises },
      { "direct"sv, TokenId::OutDirect },
      { "helper"sv, TokenId::Helper },
      { "async"sv, TokenId::Async },
      { "const"sv, TokenId::Const },
      { "module"sv, TokenId::Module },
      { "import"sv, TokenId::Import },
    };

    static constexpr Map<std::string_view, TokenId,
      sizeof(alphabet) / sizeof(std::pair<std::string_view, TokenId>)> map{ alphabet };

    auto const str = std::string_view(begin, ptr_);

    if (auto o = map.find(str); o) {
      return Token(o.value().second, {}, o.value().first, tok_line, tok_col);
    }

    if (str == "true") return Token(TokenId::Number, "1", tok_line, tok_col);
    if (str == "false") return Token(TokenId::Number, "0", tok_line, tok_col);

    return Token(TokenId::Identifier, std::string(str), tok_line, tok_col);
  }
public:
  int col() const noexcept override { return col_; }
  int line() const noexcept override { return line_; }

  // Accessor for Parser to create child lexers for imports
  ISourceProvider& get_source_provider() override {
    return source_provider_;
  }

  Token tok() override {
    skip_wp();
    int tok_line = line_;
    int tok_col = col_;

    switch (cur()) {
    case '\0': return Token(TokenId::Eof, tok_line, tok_col);
    case ':':
      next();
      if (cur() == ':') {
        next();
        return Token(TokenId::DoubleColon, tok_line, tok_col);
      }
      return Token(TokenId::Colon, tok_line, tok_col);

#define TOKEN_FUNC(x, y) case y: next(); return Token(TokenId::x, tok_line, tok_col);
      ONE_CHAR_TOKENS()
#undef TOKEN_FUNC

    case '/':
      next();
      if (cur() == '/') {
        skip_line_comment();
      } else if (cur() == '*') {
        next();
        skip_comments();
      } else {
        throw lexical_error(ctx_.current_file_path(), line_, col_, "Unknown token '/'.");
      }
      return tok();
    case quote_char: {
      next();
      const char* begin = ptr_;
      while (cur() != quote_char) {
        if (cur() == '\0' || cur() == '\n') {
          throw lexical_error(ctx_.current_file_path(), line_, col_, "Unterminated quoted string.");
        }
        next();
      }
      std::string str(begin, ptr_ - begin);
      assert(cur() == quote_char);
      next(); // skip closing quote
      return Token(TokenId::QuotedString, str, tok_line, tok_col);
    }
    default:
      if (is_digit(cur())) {
        return Token(TokenId::Number, read_number(), tok_line, tok_col);
      } else if (is_letter_or_underscore(cur())) {
        return read_string(tok_line, tok_col);
      } else {
        using namespace std::string_literals;
        throw lexical_error(ctx_.current_file_path(), line_, col_, "Unknown token '"s + cur() + "'");
      }
    };
  }

  // Single constructor - source provider injected
  Lexer(ISourceProvider& source_provider, Context& ctx) 
    : source_provider_(source_provider)
    , ctx_(ctx)
    , text_(source_provider.read_file(ctx.get_file_path()))
  {
    text_ += '\0';
    ptr_ = text_.c_str();
  }
};

template<typename T, size_t MaxSize>
class Queue {
  T* begin_;
  T* end_;
  size_t size_;
  std::aligned_storage_t<sizeof(T), alignof(T)> storage_[MaxSize];

  T* data() noexcept {
    return std::launder(reinterpret_cast<T*>(&storage_[0]));
  }
public:
  void push_back(const T& t) noexcept {
    assert(size() < MaxSize);
    if (end_ == data() + MaxSize) {
      end_ = data();
    }
    new (end_++) T(t);
    size_++;
  }

  void push_back(T&& t) noexcept {
    assert(size() < MaxSize);
    if (end_ == data() + MaxSize) {
      end_ = data();
    }
    new (end_++) T(std::move(t));
    size_++;
  }

  T&& pop_front() noexcept {
    assert(size() > 0);
    if (begin_ == data() + MaxSize) {
      begin_ = data();
    }
    size_--;
    return std::move(*begin_++);
  }

  T& back() noexcept {
    assert(size() > 0);
    return *(end_ - 1);
  }

  size_t size() const noexcept { return size_; }

  void clear() noexcept {
    size_ = 0;
    begin_ = end_ = data();
  }

  T& operator[](size_t ix) {
    int64_t diff = (data() + MaxSize) - (begin_ + ix);
    return diff > 0 ? *(begin_ + ix) : *(data() + ((int64_t)ix + diff));
  }

  Queue() { clear(); }
};

#define throw_error(msg) \
  throw parser_error(ctx_.current_file_path(), lex_.line(), lex_.col(), msg);

#define throw_unexpected_token(tok) \
  throw parser_error(ctx_.current_file_path(), lex_.line(), lex_.col(), \
    "Unexpected token: '" + std::string(tok.to_string_view()) + '\'');

// Simple Recursive Descent Parser
// Every rule is called via Parser::check() which saves/restores lookahead state
//
// NPIDL Grammar (EBNF):
//   file           ::= stmt_decl* EOF
//   stmt_decl      ::= const_decl | namespace_decl | attributes_decl? (interface_decl | struct_decl) 
//                      | enum_decl | alias_decl | module_decl | ';'
//   module_decl    ::= 'module' (IDENTIFIER '.')* IDENTIFIER ';'
//   const_decl     ::= 'const' IDENTIFIER '=' NUMBER ';'
//   namespace_decl ::= 'namespace' IDENTIFIER '{' stmt_decl* '}'
//   alias_decl     ::= 'alias' IDENTIFIER '=' type_decl ';'
//   enum_decl      ::= 'enum' IDENTIFIER (':' fundamental_type)? '{' enum_item (',' enum_item)* '}'
//   enum_item      ::= IDENTIFIER ('=' NUMBER)?
//   interface_decl ::= 'interface' IDENTIFIER (':' IDENTIFIER (',' IDENTIFIER)*)? '{' function_decl* '}'
//   struct_decl    ::= '('message' | 'exception') IDENTIFIER '{' (field_decl ';' | version_decl)* '}'
//   function_decl  ::= ('async' | type_decl) IDENTIFIER '(' (arg_decl (',' arg_decl)*)? ')' 
//                      (';' | 'raises' '(' IDENTIFIER ')' ';')
//   field_decl     ::= (IDENTIFIER | 'message') '?'? ':' type_decl
//   arg_decl       ::= (IDENTIFIER | 'message') '?'? ':' ('in' | 'out' 'direct'?) type_decl
//   type_decl      ::= fundamental_type array_decl? | IDENTIFIER array_decl?
//                      | 'vector' '<' type_decl '>' | 'string' array_decl? | 'void' | 'object'
//   array_decl     ::= '[' (IDENTIFIER | NUMBER) ']'
//   version_decl   ::= '#' 'version' NUMBER
//   attributes_decl::= '[' (IDENTIFIER '=' (IDENTIFIER | NUMBER))? ']' attributes_decl?
//
class Parser : public IParser {
  friend struct PeekGuard;
  Lexer& lex_;

  static constexpr int64_t max_lookahead_tok_n = 5;
  Queue<Token, max_lookahead_tok_n> tokens_;

  bool done_ = false;
  size_t tokens_looked_ = 0;
  Context& ctx_;
  builders::BuildGroup& builder_;
  IImportResolver& import_resolver_;
  IErrorHandler& error_handler_;

  // Error recovery support (no longer stores errors - delegated to error_handler_)
  bool panic_mode_ = false;

  using attributes_t = boost::container::small_vector<std::pair<std::string, std::string>, 2>;

  class PeekGuard {
    Parser& parser_;
    size_t saved_;
    bool discard_;
  public:
    PeekGuard(Parser& parser)
      : parser_(parser)
      , saved_(parser.tokens_looked_)
      , discard_(false) {}

    ~PeekGuard() {
      if (!discard_)
        parser_.tokens_looked_ = saved_;
    }

    void flush() {
      discard_ = true;
      parser_.flush();
    }
  };

  // peek is a one confusing method!
  Token& peek() {
    assert(tokens_looked_ < max_lookahead_tok_n);
    Token* tok;
    if (tokens_looked_ >= tokens_.size()) {
      tokens_.push_back(lex_.tok());
      tok = &tokens_.back();
    } else {
      tok = &tokens_[tokens_looked_];
    }
    tokens_looked_++;
    return *tok;
  }

  Token match(TokenId id) {
    Token t;

    if (tokens_.size() == 0) {
      t = lex_.tok();
    } else {
      t = tokens_.pop_front();
    }

    if (t != id) {
      throw_unexpected_token(t);
    }

    return t;
  }

  Token match(char id) {
    return match(static_cast<TokenId>(id));
  }

  void flush() {
    tokens_looked_ = 0;
    tokens_.clear();
  }

  template<class MemFn, typename... Args>
  bool check(MemFn Pm, Args&&... args) {
    auto const saved = tokens_looked_;
    if (std::invoke(Pm, this, std::forward<Args>(args)...))
      return true;
    tokens_looked_ = saved;
    return false;
  }

  template<class MemFn, typename... Args>
  bool maybe(bool& result, MemFn Pm, Args&&... args) {
    result = check(Pm, std::forward<Args>(args)...);
    return true;
  }

  AstNumber parse_number(const Token& tok) {
    const auto& str = tok.name;

    std::int64_t i64;
    float flt32;
    double flt64;

    if (str.size() > 2 && str[0] == '0' && str[1] == 'x') {
      auto result = std::from_chars(str.data() + 2, str.data() + str.size(), i64, 16);
      if (result.ec != std::errc()) { 
        throw_error("Hex number.std::from_chars returned: " + std::make_error_code(result.ec).message()); 
      }
      return AstNumber{ i64, NumberFormat::Hex };
    } else if(str.find('.') != std::string::npos) {
      if (str.back() == 'f') {
        auto result = std::from_chars(str.data(), str.data() + str.size(), flt32);
        if (result.ec != std::errc()) {
          throw_error("Float32 number.std::from_chars returned: " + std::make_error_code(result.ec).message());
        }
        return AstNumber{ flt32, NumberFormat::Decimal };
      } else {
        auto result = std::from_chars(str.data(), str.data() + str.size(), flt64);
        if (result.ec != std::errc()) {
          throw_error("Float64 number.std::from_chars returned: " + std::make_error_code(result.ec).message());
        }
        return AstNumber{ flt64, NumberFormat::Decimal };
      }
    } else {
      auto result = std::from_chars(str.data(), str.data() + str.size(), i64, 10);
      if (result.ec != std::errc()) {
        throw_error("Integer number.std::from_chars returned: " + std::make_error_code(result.ec).message());
      }
      return AstNumber{ i64, NumberFormat::Decimal };
    }
  }

  // Helper to convert Token to SourcePosition
  SourcePosition token_position(const Token& tok) const {
    return SourcePosition(tok.line, tok.col);
  }

  // Helper to get current lexer position
  SourcePosition current_position() const {
    return SourcePosition(lex_.line(), lex_.col());
  }

  // Helper to set position on AST node (when we have start token and know current end)
  template<typename T>
  void set_node_position(T* node, const Token& start_token) {
    node->set_position(token_position(start_token), current_position());
  }

  // array_decl ::= '[' (IDENTIFIER | NUMBER) ']'
  bool array_decl(AstTypeDecl*& type) {
    if (peek() == TokenId::SquareBracketOpen) {
      flush();

      int64_t length = -1;
      auto tok = peek();
      if (tok == TokenId::Identifier) {
        auto value = ctx_.nm_cur()->find_constant(tok.name);
        if (!value) {
          throw_error("Unknown variable: \"" + tok.name + "\"");
        } else if (!value->is_decimal()) {
          throw_error("\"" + tok.name + "\" is not a decimal constant" );
        }
        length = value->decimal();
      } else if (tok == TokenId::Number) {
        length = std::atoi(match(TokenId::Number).name.c_str());
            } else {
              throw_error("Expected a number or a constant name");
            }

      flush();

      match(TokenId::SquareBracketClose);
      type = new AstArrayDecl(type, static_cast<int>(length));

      return true;
    }
    return false;
  }

  // is_double_colon ::= '::'
  bool is_double_colon() {
    return peek() == TokenId::DoubleColon;
  }

  // get_type_namespace ::= '::' namespace_path | parent_namespace_path
  // namespace_path ::= IDENTIFIER ('::' IDENTIFIER)*
  bool get_type_namespace(Namespace*& nm, Token& type) {
    auto parse_namespace = [&]() {
      do {
        type = match(TokenId::Identifier);
        if (auto next = nm->find_child(type.name); next) {
          match(TokenId::DoubleColon);
          nm = next;
        } else {
          return;
        }
        flush();
      } while (check(&Parser::is_double_colon));
      type = match(TokenId::Identifier);
    };

    auto first_tok = peek();

    if (first_tok == TokenId::DoubleColon) {
      flush();
      nm = ctx_.nm_root();
      parse_namespace();

      return true;
    }

    auto nm_parent = nm->parent();
    if (!nm_parent) return false;

    if ((nm_parent = nm_parent->find_child(first_tok.name))) {
      flush();
      match(TokenId::DoubleColon);
      nm = nm_parent;
      parse_namespace();

      return true;
    }

    return false;
  }

  // type_decl ::= fundamental_type array_decl?
  //             | IDENTIFIER array_decl?
  //             | 'vector' '<' type_decl '>'
  //             | 'string' array_decl?
  //             | 'void'
  //             | 'object'
  // type_decl without type reference tracking (for recursive/vector types)
  bool type_decl(AstTypeDecl*& type) {
    Token dummy;
    return type_decl1(type, dummy);
  }
  
  // type_decl with type reference tracking
  bool type_decl1(AstTypeDecl*& type, Token& type_ref_token) {
    type = nullptr;
    type_ref_token = Token{}; // Reset

    auto nm = ctx_.nm_cur();
    Token t;

    if (check(&Parser::get_type_namespace, std::ref(nm), std::ref(t))) {
      flush();
    } else {
      t = peek();
    }

    if (t.is_fundamental_type()) {
      flush();
      type = new AstFundamentalType(t.id);
      check(&Parser::array_decl, std::ref(type));
      return true;
    }

    switch (t.id) {
    case TokenId::Identifier:
      type_ref_token = t; // Capture the type reference token
      flush();
      type = nm->find_type(t.name, false);
      if (!type) {
        throw_error("Unknown type '" + t.name + "'");
      }
      check(&Parser::array_decl, std::ref(type));
      return true;
    case TokenId::Vector:
      flush();
      type = new AstVectorDecl();
      match('<');
      if (!check(&Parser::type_decl, std::ref(static_cast<AstVectorDecl*>(type)->type)))
        throw_error("Expected a type declaration");
      match('>');
      return true;
    case TokenId::String:
      flush();
      type = new AstStringDecl();
      check(&Parser::array_decl, std::ref(type));
      return true;
    case TokenId::Void:
      flush();
      type = new AstVoidDecl();
      return true;
    case TokenId::Object:
      flush();
      type = new AstObjectDecl();
      return true;
    default:
      return false;
    }
  }

  // field_decl ::= IDENTIFIER '?'? ':' type_decl
  bool field_decl(AstFieldDecl*& field) {
    Token field_name;
    AstTypeDecl* type;
    Token type_token;
    bool optional;

    field_name = peek();
    if (!(
          (field_name == TokenId::Identifier || field_name == TokenId::Message)
        && maybe(optional, &Parser::one, TokenId::Optional)
        && peek() == TokenId::Colon 
        && check(&Parser::type_decl1, std::ref(type), std::ref(type_token))
      )) return false;
    
    if (field_name == TokenId::Message)
      field_name.name = "message";

    field = new AstFieldDecl();
    field->name = std::move(field_name.name);
    field->type = optional ? new AstOptionalDecl(type) : type;

    // Set position for just the field name token
    SourcePosition start = token_position(field_name);
    SourcePosition end(field_name.line, field_name.col + static_cast<int>(field->name.length()) - 1);
    field->range = SourceRange(start, end);
    
    // Set type reference position if we got a valid identifier token
    if (type_token.id == TokenId::Identifier) {
      SourcePosition type_start = token_position(type_token);
      SourcePosition type_end(type_token.line, type_token.col + static_cast<int>(type_token.name.length()) - 1);
      field->type_ref_range = SourceRange(type_start, type_end);
    }

    return true;
  }

  // arg_decl ::= IDENTIFIER '?'? ':' ('in' | 'out' 'direct'?) type_decl
  bool arg_decl(AstFunctionArgument& arg, Token& start_token, Token& type_token) {
    Token arg_name = peek();
    bool optional;

    if (!(
           (arg_name == TokenId::Identifier  || arg_name == TokenId::Message)
        && maybe(optional, &Parser::one, TokenId::Optional) 
        && peek() == TokenId::Colon
      )) return false;

    if (arg_name == TokenId::Message)
      arg_name.name = "message";

    // Save start token for position tracking
    start_token = arg_name;

    if (check(&Parser::one, TokenId::In)) arg.modifier = ArgumentModifier::In;
    else if (check(&Parser::one, TokenId::Out)) arg.modifier = ArgumentModifier::Out;
    else {
      throw_error("Expected 'in' or 'out' keywords after parameter name declaration.");
    }

    if (arg.modifier == ArgumentModifier::Out && check(&Parser::one, TokenId::OutDirect)) {
      arg.direct = true;
    }

    AstTypeDecl* type;

    if (!check(&Parser::type_decl1, std::ref(type), std::ref(type_token)))
      return false;

    arg.name = arg_name.name;
    arg.type = optional ? new AstOptionalDecl(type) : type;

    return true;
  }

  // version_decl ::= '#' 'version' NUMBER
  bool version_decl(AstStructDecl* s) {
    if (!(peek() == TokenId::Hash && peek() == TokenId::Identifier && tokens_.back().name == "version")) return false;

    flush();

    auto vn = match(TokenId::Number);

    if (s->version != -1) {
      throw_error("version redefinition.");
    }

    s->version = std::atoi(vn.name.c_str());
    return true;
  }

  // Helper for struct parsing
  bool struct_close_tag() {
    if (peek() == TokenId::BracketClose) {
      flush();
      return true;
    }
    return false;
  }

  // struct_decl ::= ('message' | 'exception') IDENTIFIER '{' (field_decl ';' | version_decl)* '}'
  bool struct_decl(attributes_t& attr) {
    auto first_tok = peek();
    if (first_tok != TokenId::Message && first_tok != TokenId::Exception)
      return false;

    flush();

    Token name_tok = match(TokenId::Identifier);
    auto s = new AstStructDecl();
    bool is_exception = first_tok == TokenId::Exception;
    s->name = name_tok.name;
    if (is_exception) {
      s->exception_id = ctx_.next_exception_id();
      ctx_.exceptions.push_back(s);
    } else {
      s->exception_id = -1;
    }

    s->nm = ctx_.nm_cur();

    if (is_exception) {
      auto ex_id = new AstFieldDecl();
      ex_id->name = "__ex_id";
      ex_id->type = new AstFundamentalType(TokenId::UInt32);
      s->fields.push_back(ex_id);
    }

    match('{');

    while (check(&Parser::struct_close_tag) == false) {
      AstFieldDecl* field;
      if (check(&Parser::field_decl, std::ref(field))) {
        s->fields.push_back(field);
        s->flat &= is_flat(field->type);
        match(';');
      } else if (check(&Parser::version_decl, s)) {
        // ok
      } else {
        throw_error("Syntax error");
      }
    }

    calc_struct_size_align(s);

    // Set position range for the struct declaration
    set_node_position(s, name_tok);

    builder_.emit((!s->is_exception() ? &builders::Builder::emit_struct : &builders::Builder::emit_exception), s);
    ctx_.nm_cur()->add(s->name, s);

    for (const auto& a : attr) {
      if (a.first == "force_helpers" && a.second == "1")
        ctx_.mark_struct_as_having_helpers(s);
      else
        throw_error("Unknown attribute: " + a.first + " for struct " + s->name);
    }

    return true;
  }

  // const_decl ::= 'const' IDENTIFIER '=' NUMBER ';'
  bool const_decl() {
    if (peek() != TokenId::Const) return false;
    flush();
    auto var_name = match(TokenId::Identifier); match('=');
    auto var_value = parse_number(match(TokenId::Number)); match(';');
    builder_.emit(&builders::Builder::emit_constant, var_name.name, &var_value);
    ctx_.nm_cur()->add_constant(std::move(var_name.name), std::move(var_value));
    return true;
  }

  // module_decl ::= 'module' (IDENTIFIER '.')* IDENTIFIER ';'
  bool module_decl() {
    if (peek() != TokenId::Module)
      return false;

    // Option 4: Module is only allowed in main file, not in imports
    if (ctx_.is_in_import()) {
      auto tok = peek();
      throw_error("Module declaration not allowed in imported files");
    }

    flush();

    Token tok;
    std::vector<std::string> module_parts;
    for (tok = peek(); tok == TokenId::Identifier || tok == '.'; tok = peek()) {
      flush();
      if (tok == TokenId::Identifier)
        module_parts.push_back(std::move(tok.name));
    }

    if (tok == '.')
      throw_error("Expected module name after '.'");

    if (tok != TokenId::Semicolon)
      throw_error("Expected ';' at the end of module declaration");

    if (module_parts.empty())
      throw_error("Expected module name");

    flush();

    ctx_.set_module_name(std::move(module_parts));
    return true;
  }

  // namespace_decl ::= 'namespace' IDENTIFIER '{' stmt_decl* '}'
  bool namespace_decl() {
    if (peek() != TokenId::Namespace) return false;
    flush();

    auto name = match(TokenId::Identifier); match('{');
    ctx_.push_namespace(std::move(name.name));

    builder_.emit(&builders::Builder::emit_namespace_begin);

    for (;;) {
      if (check(&Parser::one, TokenId::BracketClose)) {
        builder_.emit(&builders::Builder::emit_namespace_end);
        ctx_.pop_namespace();
        break;
      }
      if (!check(&Parser::stmt_decl)) {
        throw_error("expected statement declaration inside namespace");
      }
    }
    return true;
  }

  // import_decl ::= 'import' STRING ';'
  bool import_decl() {
    auto import_tok = peek();
    if (import_tok != TokenId::Import)
      return false;

    flush();

    auto import_path_tok = match(TokenId::QuotedString);
    match(';');

    // Create import AST node
    auto* import = new AstImportDecl();
    import->import_path = import_path_tok.name;
    import->import_line = import_tok.line;
    import->import_col = import_tok.col;
    import->path_start_line = import_path_tok.line;
    import->path_start_col = import_path_tok.col;
    import->path_end_col = import_path_tok.col + (int)import_path_tok.name.length() + 2; // +2 for quotes

    // Use injected resolver to resolve import path
    auto resolved = import_resolver_.resolve_import(
        import->import_path,
        ctx_.get_file_path()
    );

    if (resolved) {
        import->resolved_path = *resolved;
        import->resolved = true;

        // Check if we should parse this import (depends on resolver strategy)
        if (import_resolver_.should_parse_import(*resolved)) {
            // Parse the imported file
            try {
                // Push file context - tracks that we're now in an import
                FileContextGuard guard(ctx_, *resolved);

                // Create a new lexer for the imported file
                Lexer import_lexer(lex_.get_source_provider(), ctx_);

                // Create a new parser for the imported file
                // Reuse same builder, import_resolver, and error_handler
                Parser import_parser(
                    import_lexer,
                    ctx_,
                    builder_,
                    import_resolver_,
                    error_handler_
                );

                // Parse the imported file
                import_parser.parse();

                // FileContextGuard destructor automatically pops file context
            } catch (const parser_error& e) {
                import->resolved = false;
                import->error_message = std::string("Error parsing imported file: ") + e.what();

                // Re-throw or handle based on error recovery mode
                if (!error_handler_.should_continue_after_error()) {
                    throw;
                }
                error_handler_.handle_error(e);
            }
        }
    } else {
        import->resolved = false;
        import->error_message = "Cannot resolve import path";

        // Report error through error handler
        error_handler_.handle_error(parser_error(
            ctx_.current_file_path(),
            import_tok.line,
            import_tok.col,
            "Cannot resolve import: " + import->import_path
        ));
    }

    ctx_.imports.push_back(import);

    return true;
  }

  // function_decl ::= ('async' | type_decl) IDENTIFIER '(' (arg_decl (',' arg_decl)*)? ')' (';' | 'raises' '(' IDENTIFIER ')' ';')
  bool function_decl(AstFunctionDecl*& f) {
    bool is_async;
    AstTypeDecl* ret_type = nullptr;
    Token start_tok;
    {
      PeekGuard pg(*this);
      start_tok = peek(); // Capture starting position (async or return type)
    }
    if (!(is_async = check(&Parser::one, TokenId::Async)) && 
      !check(&Parser::type_decl, std::ref(ret_type))) return false;

    if (!ret_type) ret_type = new AstVoidDecl();

    f = new AstFunctionDecl();
    f->ret_value = ret_type ;
    f->is_async = is_async;
    auto name_tok = match(TokenId::Identifier);
    f->name = name_tok.name;

    match('(');

    AstFunctionArgument arg;
    Token arg_start;
    Token arg_type_token;

    if (check(&Parser::one, TokenId::RoundBracketClose) == false) {
      for (;;) {
        if (!check(&Parser::arg_decl, std::ref(arg), std::ref(arg_start), std::ref(arg_type_token))) {
          throw_error("Expected tokens: argument declaration");
        }
        auto* arg_ptr = new AstFunctionArgument(std::move(arg));
        // Set position for just the argument name
        SourcePosition start = token_position(arg_start);
        SourcePosition end(arg_start.line, arg_start.col + static_cast<int>(arg_ptr->name.length()) - 1);
        arg_ptr->range = SourceRange(start, end);
        
        // Set type reference position if we got a valid identifier token
        if (arg_type_token.id == TokenId::Identifier) {
          SourcePosition type_start = token_position(arg_type_token);
          SourcePosition type_end(arg_type_token.line, arg_type_token.col + static_cast<int>(arg_type_token.name.length()) - 1);
          arg_ptr->type_ref_range = SourceRange(type_start, type_end);
        }
        
        f->args.push_back(arg_ptr);
        if (check(&Parser::one, TokenId::RoundBracketClose)) break;
        match(',');
      }
    }

    auto tok = peek();

    if (tok == TokenId::Semicolon) {
      flush();
    } else if (tok == TokenId::Raises) {
      if (f->is_async) throw_error("function declared as async cannot throw exceptions");

      flush();
      match('(');

      auto type = ctx_.nm_cur()->find_type(match(TokenId::Identifier).name, false);
      if (!type) throw_error("unknown exception type");

      if (type->id != FieldType::Struct && cflat(type)->is_exception()) {
        throw_error("class is not an exception");
      }

      f->ex = cflat(type);

      match(')'); match(';');
    } else {
      throw_unexpected_token(tok);
    }

    // Set position for the function declaration
    set_node_position(f, start_tok);

    return true;
  }

  // attributes_decl ::= '[' (IDENTIFIER '=' (IDENTIFIER | NUMBER))? ']' attributes_decl?
  bool attributes_decl(boost::container::small_vector<std::pair<std::string, std::string>, 2>& attr) {
    if (peek() != '[') return false;
    flush();

    {
      PeekGuard pg(*this);
      if (peek() == ']') {
        pg.flush();
        goto attributes_decl_next;
      }
    }

    {
      PeekGuard pg(*this);

      if (auto attribute = peek(); attribute == TokenId::Identifier) {
        pg.flush();
        match('=');
        Token value = peek();
        if (!(value == TokenId::Identifier || value == TokenId::Number)) {
          throw_error("Expected identifier or number");
        }
        flush();
        match(']');

        attr.emplace_back(attribute.name, value.name);
        goto attributes_decl_next;
      }
    }

    throw_unexpected_token(peek());

  attributes_decl_next:
    check(&Parser::attributes_decl, std::ref(attr));

    return true;
  }

  // interface_decl ::= 'interface' IDENTIFIER (':' IDENTIFIER (',' IDENTIFIER)*)? '{' function_decl* '}'
  bool interface_decl(attributes_t& attr) {

    auto start_tok = peek();
    if (start_tok != TokenId::Interface) return false;
    flush();

    auto ifs = new AstInterfaceDecl();
    auto name_tok = match(TokenId::Identifier);
    ifs->name = name_tok.name;

    for (const auto& a : attr) {
      if (a.first == "trusted")
        ifs->trusted = (a.second == "1");
      else
        throw_error("Unknown attribute: " + a.first + " for interface " + ifs->name);
      // other attributes...
    }

    {
      PeekGuard pg(*this);
      if (peek() == TokenId::Colon) {
        pg.flush();

        for (;;) {
          auto name = match(TokenId::Identifier).name;
          if (auto type = ctx_.nm_cur()->find_type(name, false); type && type->id == FieldType::Interface) {
            ifs->plist.push_back(cifs(type));
          } else {
            throw_error("Unknown interface");
          }
          {
            PeekGuard pg(*this);
            if (peek() == TokenId::Comma) {
              pg.flush();
            } else {
              break;
            }
          }
        }
      }
    }

    match('{');

    AstFunctionDecl* f;
    for (;;) {
      if (check(&Parser::one, TokenId::BracketClose)) break;
      if (check(&Parser::function_decl, std::ref(f))) {
        ifs->fns.emplace_back(f);
        continue;
      }
      throw_error("Expected tokens: function declaration, '}' ");
    }

    for (uint16_t idx = 0; idx < ifs->fns.size(); ++idx) {
      ifs->fns[idx]->idx = idx;
    }

    // Set position range for the interface declaration
    set_node_position(ifs, start_tok);

    ctx_.interfaces.push_back(ifs);
    ctx_.nm_cur()->add(ifs->name, ifs);
    builder_.emit(&builders::Builder::emit_interface, ifs);

    return true;
  }

  // one ::= TOKEN_ID (helper to match a single token)
  bool one(TokenId id) {
    if (peek() == id) { flush(); return true; }
    return false;
  }

  // eof ::= EOF
  bool eof() {
    if (peek() == TokenId::Eof) { flush(); return (done_ = true); }
    return false;
  }

  // alias_decl ::= 'alias' IDENTIFIER '=' type_decl ';'
  bool alias_decl() {
    Token start_tok;
    Token left;

    if (!(
      (start_tok = peek()) == TokenId::Alias &&
      (left = peek()) == TokenId::Identifier &&
      peek() == TokenId::Assignment
      ))
    {
      return false;
    }

    AstTypeDecl* right;
    if (!check(&Parser::type_decl, std::ref(right))) {
      throw_error("Expected a type declaration after using");
    }

    auto a = new AstAliasDecl(std::move(left.name), ctx_.nm_cur(), right);

    // Set position for the using declaration
    set_node_position(a, start_tok);

    ctx_.nm_cur()->add(a->name, a);

    builder_.emit(&builders::Builder::emit_using, a);

    return true;
  }

  // enum_decl ::= 'enum' IDENTIFIER (':' fundamental_type)? '{' enum_item (',' enum_item)* '}'
  // enum_item ::= IDENTIFIER ('=' NUMBER)?
  bool enum_decl() {
    auto start_tok = peek();
    if (start_tok != TokenId::Enum) return false;
    flush();

    auto e = new AstEnumDecl;

    e->name = std::move(match(TokenId::Identifier).name);
    e->token_id = TokenId::UInt32;
    e->nm = ctx_.nm_cur();

    {
      PeekGuard g(*this);
      Token tok = peek();
      if (tok == TokenId::Colon) {
        tok = peek();
        if (tok.is_fundamental_type() == false ||
          tok == TokenId::Float32 ||
          tok == TokenId::Float64 ||
          tok == TokenId::Boolean) {
          throw_error("expected a numeric type");
        }
        e->token_id = tok.id;
        g.flush();
      }
    }

    const int max_size = get_fundamental_size(e->token_id);

    match('{');

    Token tok;
    int64_t ix = 0;

    while ((tok = peek()) != TokenId::BracketClose) {
      if (tok.id != TokenId::Identifier) throw_error("Unexpected token '" + tok.name + '\'');

      auto name = std::move(tok.name);

      tok = peek();

      if (tok.id == TokenId::Assignment) {
        flush();
        auto n = parse_number(match(TokenId::Number));
        n.max_size = max_size;

        if (!std::holds_alternative<std::int64_t>(n.value)) {
          throw_error("Floating types are not allowed.");
        }

        // explicit
        e->items.emplace_back(std::move(name), std::pair<AstNumber, bool>{ n, true });

        ix = std::get<std::int64_t>(n.value) + 1;
        tok = peek();
      } else {

        // implicit
        e->items.emplace_back(std::move(name), std::pair<AstNumber, bool>{ ix, 0 });
        ix++;
      }

      if (tok == TokenId::BracketClose)
        break;

      if (tok != TokenId::Comma)
        throw_error("Unexpected token '" + tok.name + '\'');

      flush();
    }

    flush();

    // Set position range for the enum declaration
    set_node_position(e, start_tok);

    ctx_.nm_cur()->add(e->name, e);

    builder_.emit(&builders::Builder::emit_enum, e);

    return true;
  }

  // Helper to parse declarations that can have attributes
  bool something_that_could_have_attributes() {
    attributes_t attr;
    check(&Parser::attributes_decl, std::ref(attr));

    return check(&Parser::interface_decl, std::ref(attr)) ||
      check(&Parser::struct_decl, std::ref(attr));
  }

  // stmt_decl ::= const_decl | namespace_decl | attributes_decl? (interface_decl | struct_decl) | enum_decl | alias_decl | module_decl | ';'
  bool stmt_decl() {
    return (
      check(&Parser::import_decl) ||
      check(&Parser::const_decl) ||
      check(&Parser::namespace_decl) ||
      check(&Parser::something_that_could_have_attributes) ||
      check(&Parser::enum_decl) ||
      check(&Parser::alias_decl) ||
      check(&Parser::module_decl) ||
      check(&Parser::one, TokenId::Semicolon)
      );
  }

  // Error recovery support
  void record_error(const parser_error& e) {
    error_handler_.handle_error(e);
    panic_mode_ = true;
  }

  void synchronize() {
    // Skip tokens until we find a synchronization point
    // Good sync points: semicolon, closing brace, namespace, interface, struct
    panic_mode_ = false;
    flush();

    while (true) {
      Token t;
      if (tokens_.size() == 0) {
        t = lex_.tok();
      } else {
        t = tokens_.pop_front();
      }

      // Synchronization points - safe places to resume parsing
      if (t == TokenId::Semicolon ||
          t == TokenId::BracketClose ||
          t == TokenId::Eof) {
        break;
      }

      // Look ahead - if next token is a statement starter, stop here
      Token next = peek();
      if (next == TokenId::Namespace ||
          next == TokenId::Interface ||
          next == TokenId::Message ||
          next == TokenId::Exception ||
          next == TokenId::Enum ||
          next == TokenId::Alias ||
          next == TokenId::Const ||
          next == '[') {  // Attributes
        break;
      }
      flush();
    }
  }

  // Wrapper to handle errors with recovery
  template<typename Fn>
  bool try_parse(Fn&& fn, const char* error_msg) {
    if (!error_handler_.should_continue_after_error()) {
      return fn();
    }

    try {
      return fn();
    } catch (parser_error& e) {
      record_error(e);
      synchronize();
      return false;
    } catch (lexical_error& e) {
      record_error(parser_error(ctx_.current_file_path(), e.line, e.col, e.what()));
      synchronize();
      return false;
    }
  }

public:
  // Single constructor - dependencies injected
  Parser(
    Lexer& lex,
    Context& ctx,
    builders::BuildGroup& builder,
    IImportResolver& import_resolver,
    IErrorHandler& error_handler
  )
    : lex_(lex)
    , ctx_(ctx)
    , builder_(builder)
    , import_resolver_(import_resolver)
    , error_handler_(error_handler)
  {
  }

  void parse() override {
    if (!error_handler_.should_continue_after_error()) {
      // Original behavior - throw on first error
      while (!done_) {
        if (!(
          check(&Parser::stmt_decl) ||
          check(&Parser::eof)
          )) {
          throw_error("Expected tokens: statement, eof");
        }
      }
    } else {
      // Error recovery mode - collect all errors
      while (!done_) {
        try_parse([this]() {
          if (check(&Parser::stmt_decl) || check(&Parser::eof)) {
            return true;
          }
          throw_error("Expected tokens: statement, eof");
          return false;
        }, "Statement parsing failed");
      }
    }
  }
};

// Parse into existing context (for LSP with persistent AST)
bool parse_for_lsp(Context& ctx, const std::string& content, std::vector<ParseError>& errors) {
  errors.clear();

  try {
    builders::BuildGroup builder(&ctx);

    // Use test parser factory for in-memory content
    auto [source_provider, import_resolver, error_handler, lexer, parser] = 
      ParserFactory::create_test_parser(ctx, builder, content);

    parser->parse();

    // Collect any errors that occurred
    for (const auto& e : error_handler->get_errors()) {
      ParseError err;
      err.line = e.line;
      err.col = e.col;
      err.message = e.what();
      errors.push_back(err);
    }

    return error_handler->get_errors().empty();
  } catch (const std::exception& e) {
    // Fallback for unexpected errors
    ParseError err;
    err.line = 1;
    err.col = 1;
    err.message = std::string("Unexpected error: ") + e.what();
    errors.push_back(err);
    return false;
  }
}

// Parse string content for testing purposes
bool parse_string_for_testing(const std::string& content, std::vector<ParseError>& errors) {
  errors.clear();

  try {
    Context ctx;

    builders::BuildGroup builder(&ctx);

    // Use test parser factory for in-memory content
    auto [source_provider, import_resolver, error_handler, lexer, parser] = 
      ParserFactory::create_test_parser(ctx, builder, content);

    parser->parse();

    // Collect any errors that occurred
    for (const auto& e : error_handler->get_errors()) {
      ParseError err;
      err.line = e.line;
      err.col = e.col;
      err.message = e.what();
      errors.push_back(err);
    }

    return error_handler->get_errors().empty();
  } catch (const std::exception& e) {
    // Fallback for unexpected errors
    ParseError err;
    err.line = 1;
    err.col = 1;
    err.message = std::string("Unexpected error: ") + e.what();
    errors.push_back(err);
    return false;
  }
}

// ParserFactory implementation
std::tuple<
    std::unique_ptr<FileSystemSourceProvider>,
    std::unique_ptr<CompilerImportResolver>,
    std::unique_ptr<CompilerErrorHandler>,
    std::unique_ptr<ILexer>,
    std::unique_ptr<IParser>
>
ParserFactory::create_compiler_parser(Context& ctx, builders::BuildGroup& builder) {
    auto source_provider = std::make_unique<FileSystemSourceProvider>();
    auto import_resolver = std::make_unique<CompilerImportResolver>();
    auto error_handler = std::make_unique<CompilerErrorHandler>();

    auto lexer = std::make_unique<Lexer>(*source_provider, ctx);
    auto parser = std::make_unique<Parser>(
        *lexer,
        ctx,
        builder,
        *import_resolver,
        *error_handler
    );

    return {
        std::move(source_provider),
        std::move(import_resolver),
        std::move(error_handler),
        std::move(lexer),
        std::move(parser)
    };
}

std::tuple<
    std::shared_ptr<LspSourceProvider>,
    std::unique_ptr<LspImportResolver>,
    std::unique_ptr<LspErrorHandler>,
    std::unique_ptr<ILexer>,
    std::unique_ptr<IParser>
>
ParserFactory::create_lsp_parser(Context& ctx, builders::BuildGroup& builder, std::shared_ptr<LspSourceProvider> source_provider) {
    if (!source_provider) {
        source_provider = std::make_shared<LspSourceProvider>();
    }

    auto import_resolver = std::make_unique<LspImportResolver>();
    auto error_handler = std::make_unique<LspErrorHandler>();

    auto lexer = std::make_unique<Lexer>(*source_provider, ctx);
    auto parser = std::make_unique<Parser>(
        *lexer,
        ctx,
        builder,
        *import_resolver,
        *error_handler
    );

    return {
        source_provider,
        std::move(import_resolver),
        std::move(error_handler),
        std::move(lexer),
        std::move(parser)
    };
}

std::tuple<
    std::shared_ptr<InMemorySourceProvider>,
    std::unique_ptr<LspImportResolver>,
    std::unique_ptr<LspErrorHandler>,
    std::unique_ptr<ILexer>,
    std::unique_ptr<IParser>
>
ParserFactory::create_test_parser(Context& ctx, builders::BuildGroup& builder, const std::string& content) {
    auto source_provider = std::make_shared<InMemorySourceProvider>(content);

    auto import_resolver = std::make_unique<LspImportResolver>();
    auto error_handler = std::make_unique<LspErrorHandler>();

    auto lexer = std::make_unique<Lexer>(*source_provider, ctx);
    auto parser = std::make_unique<Parser>(
        *lexer,
        ctx,
        builder,
        *import_resolver,
        *error_handler
    );

    return {
        source_provider,
        std::move(import_resolver),
        std::move(error_handler),
        std::move(lexer),
        std::move(parser)
    };
}

} // namespace npidl
