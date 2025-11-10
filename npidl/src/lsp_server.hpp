// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <string>
#include <map>
#include <vector>
#include <optional>
#include <memory>
#include <iostream>
#include <sstream>
#include <glaze/glaze.hpp>

#include "workspace_manager.hpp"

// LSP Protocol Structures (subset for MVP)
namespace lsp {

struct Position {
  int line = 0;      // 0-based
  int character = 0; // 0-based
};

struct Range {
  Position start;
  Position end;
};

struct Location {
  std::string uri;
  Range range;
};

// SymbolKind enumeration
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#symbolKind
enum class SymbolKind {
  File = 1,
  Module = 2,
  Namespace = 3,
  Package = 4,
  Class = 5,
  Method = 6,
  Property = 7,
  Field = 8,
  Constructor = 9,
  Enum = 10,
  Interface = 11,
  Function = 12,
  Variable = 13,
  Constant = 14,
  String = 15,
  Number = 16,
  Boolean = 17,
  Array = 18,
  Object = 19,
  Key = 20,
  Null = 21,
  EnumMember = 22,
  Struct = 23,
  Event = 24,
  Operator = 25,
  TypeParameter = 26
};

struct DocumentSymbol {
  std::string name;
  std::optional<std::string> detail;
  SymbolKind kind;
  Range range;
  Range selectionRange;
  std::vector<DocumentSymbol> children;
};

struct Diagnostic {
  Range range;
  int severity = 1; // 1=Error, 2=Warning, 3=Info, 4=Hint
  std::string message;
  std::optional<std::string> source;
};

struct TextDocumentIdentifier {
  std::string uri;
};

struct VersionedTextDocumentIdentifier {
  std::string uri;
  int version = 0;
};

struct TextDocumentItem {
  std::string uri;
  std::string languageId;
  int version = 0;
  std::string text;
};

struct TextDocumentContentChangeEvent {
  std::optional<Range> range;
  std::string text;
};

struct DidOpenTextDocumentParams {
  TextDocumentItem textDocument;
};

struct DidChangeTextDocumentParams {
  VersionedTextDocumentIdentifier textDocument;
  std::vector<TextDocumentContentChangeEvent> contentChanges;
};

struct DidCloseTextDocumentParams {
  TextDocumentIdentifier textDocument;
};

struct TextDocumentPositionParams {
  TextDocumentIdentifier textDocument;
  Position position;
};

struct Hover {
  std::string contents; // Markdown string
  std::optional<Range> range;
};

struct DocumentSymbolParams {
  TextDocumentIdentifier textDocument;
};

struct SemanticTokensParams {
  TextDocumentIdentifier textDocument;
};

struct SemanticTokensResponse {
  std::vector<uint32_t> data; // Encoded token array
};

struct ServerCapabilities {
  struct TextDocumentSync {
    int openClose = 1;
    int change = 1; // 1=Full, 2=Incremental
  };
  
  struct SemanticTokensLegend {
    std::vector<std::string> tokenTypes;
    std::vector<std::string> tokenModifiers;
  };
  
  struct SemanticTokensOptions {
    SemanticTokensLegend legend;
    bool full = true;
    bool range = false;
  };
  
  TextDocumentSync textDocumentSync;
  bool hoverProvider = true;
  bool definitionProvider = true;
  bool referencesProvider = true;
  bool documentSymbolProvider = true;
  std::optional<SemanticTokensOptions> semanticTokensProvider;
};

struct InitializeResult {
  ServerCapabilities capabilities;
};

} // namespace lsp

// Glaze metadata for serialization
template <>
struct glz::meta<lsp::Position> {
  using T = lsp::Position;
  static constexpr auto value = object(
    "line", &T::line,
    "character", &T::character
  );
};

template <>
struct glz::meta<lsp::Range> {
  using T = lsp::Range;
  static constexpr auto value = object(
    "start", &T::start,
    "end", &T::end
  );
};

template <>
struct glz::meta<lsp::Location> {
  using T = lsp::Location;
  static constexpr auto value = object(
    "uri", &T::uri,
    "range", &T::range
  );
};

template <>
struct glz::meta<lsp::SymbolKind> {
  using enum lsp::SymbolKind;
  static constexpr auto value = enumerate(
    File, Module, Namespace, Package, Class, Method, Property, Field,
    Constructor, Enum, Interface, Function, Variable, Constant,
    String, Number, Boolean, Array, Object, Key, Null, EnumMember,
    Struct, Event, Operator, TypeParameter
  );
};

template <>
struct glz::meta<lsp::DocumentSymbol> {
  using T = lsp::DocumentSymbol;
  static constexpr auto value = object(
    "name", &T::name,
    "detail", &T::detail,
    "kind", &T::kind,
    "range", &T::range,
    "selectionRange", &T::selectionRange,
    "children", &T::children
  );
};

template <>
struct glz::meta<lsp::Diagnostic> {
  using T = lsp::Diagnostic;
  static constexpr auto value = object(
    "range", &T::range,
    "severity", &T::severity,
    "message", &T::message,
    "source", &T::source
  );
};

template <>
struct glz::meta<lsp::TextDocumentIdentifier> {
  using T = lsp::TextDocumentIdentifier;
  static constexpr auto value = object(
    "uri", &T::uri
  );
};

template <>
struct glz::meta<lsp::VersionedTextDocumentIdentifier> {
  using T = lsp::VersionedTextDocumentIdentifier;
  static constexpr auto value = object(
    "uri", &T::uri,
    "version", &T::version
  );
};

template <>
struct glz::meta<lsp::TextDocumentItem> {
  using T = lsp::TextDocumentItem;
  static constexpr auto value = object(
    "uri", &T::uri,
    "languageId", &T::languageId,
    "version", &T::version,
    "text", &T::text
  );
};

template <>
struct glz::meta<lsp::TextDocumentContentChangeEvent> {
  using T = lsp::TextDocumentContentChangeEvent;
  static constexpr auto value = object(
    "range", &T::range,
    "text", &T::text
  );
};

template <>
struct glz::meta<lsp::DidOpenTextDocumentParams> {
  using T = lsp::DidOpenTextDocumentParams;
  static constexpr auto value = object(
    "textDocument", &T::textDocument
  );
};

template <>
struct glz::meta<lsp::DidChangeTextDocumentParams> {
  using T = lsp::DidChangeTextDocumentParams;
  static constexpr auto value = object(
    "textDocument", &T::textDocument,
    "contentChanges", &T::contentChanges
  );
};

template <>
struct glz::meta<lsp::DidCloseTextDocumentParams> {
  using T = lsp::DidCloseTextDocumentParams;
  static constexpr auto value = object(
    "textDocument", &T::textDocument
  );
};

template <>
struct glz::meta<lsp::TextDocumentPositionParams> {
  using T = lsp::TextDocumentPositionParams;
  static constexpr auto value = object(
    "textDocument", &T::textDocument,
    "position", &T::position
  );
};

template <>
struct glz::meta<lsp::Hover> {
  using T = lsp::Hover;
  static constexpr auto value = object(
    "contents", &T::contents,
    "range", &T::range
  );
};

template <>
struct glz::meta<lsp::DocumentSymbolParams> {
  using T = lsp::DocumentSymbolParams;
  static constexpr auto value = object(
    "textDocument", &T::textDocument
  );
};

template <>
struct glz::meta<lsp::SemanticTokensParams> {
  using T = lsp::SemanticTokensParams;
  static constexpr auto value = object(
    "textDocument", &T::textDocument
  );
};

template <>
struct glz::meta<lsp::SemanticTokensResponse> {
  using T = lsp::SemanticTokensResponse;
  static constexpr auto value = object(
    "data", &T::data
  );
};

template <>
struct glz::meta<lsp::ServerCapabilities::TextDocumentSync> {
  using T = lsp::ServerCapabilities::TextDocumentSync;
  static constexpr auto value = object(
    "openClose", &T::openClose,
    "change", &T::change
  );
};

template <>
struct glz::meta<lsp::ServerCapabilities::SemanticTokensLegend> {
  using T = lsp::ServerCapabilities::SemanticTokensLegend;
  static constexpr auto value = object(
    "tokenTypes", &T::tokenTypes,
    "tokenModifiers", &T::tokenModifiers
  );
};

template <>
struct glz::meta<lsp::ServerCapabilities::SemanticTokensOptions> {
  using T = lsp::ServerCapabilities::SemanticTokensOptions;
  static constexpr auto value = object(
    "legend", &T::legend,
    "full", &T::full,
    "range", &T::range
  );
};

template <>
struct glz::meta<lsp::ServerCapabilities> {
  using T = lsp::ServerCapabilities;
  static constexpr auto value = object(
    "textDocumentSync", &T::textDocumentSync,
    "hoverProvider", &T::hoverProvider,
    "definitionProvider", &T::definitionProvider,
    "referencesProvider", &T::referencesProvider,
    "documentSymbolProvider", &T::documentSymbolProvider,
    "semanticTokensProvider", &T::semanticTokensProvider
  );
};

template <>
struct glz::meta<lsp::InitializeResult> {
  using T = lsp::InitializeResult;
  static constexpr auto value = object(
    "capabilities", &T::capabilities
  );
};

// JSON-RPC 2.0 Message Structures
namespace jsonrpc {

struct Message {
  std::string jsonrpc = "2.0";
};

struct Request : Message {
  glz::json_t id; // Can be number or string
  std::string method;
  glz::raw_json params; // Raw JSON for flexible parsing
};

struct Response : Message {
  glz::json_t id;
  std::optional<glz::raw_json> result;
  std::optional<glz::raw_json> error;
};

struct Notification : Message {
  std::string method;
  glz::raw_json params;
};

} // namespace jsonrpc

template <>
struct glz::meta<jsonrpc::Request> {
  using T = jsonrpc::Request;
  static constexpr auto value = object(
    "jsonrpc", &T::jsonrpc,
    "id", &T::id,
    "method", &T::method,
    "params", &T::params
  );
};

template <>
struct glz::meta<jsonrpc::Response> {
  using T = jsonrpc::Response;
  static constexpr auto value = object(
    "jsonrpc", &T::jsonrpc,
    "id", &T::id,
    "result", &T::result,
    "error", &T::error
  );
};

template <>
struct glz::meta<jsonrpc::Notification> {
  using T = jsonrpc::Notification;
  static constexpr auto value = object(
    "jsonrpc", &T::jsonrpc,
    "method", &T::method,
    "params", &T::params
  );
};

// Forward declarations
class Context;
class Parser;

// Document Management
class DocumentManager {
public:
  struct Document {
    std::string uri;
    std::string content;
    int version = 0;
    std::vector<lsp::Diagnostic> diagnostics;
    
    // Parsed imports from this file
    std::vector<std::string> imports;
    
    // Note: Context and AST are stored in ProjectContext via WorkspaceManager
  };

  void open(const std::string& uri, const std::string& text, int version);
  void change(const std::string& uri, const std::string& text, int version);
  void close(const std::string& uri);
  
  Document* get(const std::string& uri);
  
  std::vector<lsp::Diagnostic> parse_and_get_diagnostics(
    npidl::WorkspaceManager& workspace,
    Document& doc
  );

private:
  std::map<std::string, Document> documents_;
};

// LSP Server
class LspServer {
public:
  LspServer();

  void run();
private:
  DocumentManager documents_;
  npidl::WorkspaceManager workspace_;
  bool initialized_ = false;
  
  // Message I/O
  std::optional<std::string> read_message();
  void send_message(const std::string& message);
  void send_response(const glz::json_t& id, const std::string& result);
  void send_error(const glz::json_t& id, int code, const std::string& message);
  void send_notification(const std::string& method, const std::string& params);
  
  // Request handlers
  void handle_initialize(const glz::json_t& id, const glz::raw_json& params);
  void handle_initialized(const glz::raw_json& params);
  void handle_shutdown(const glz::json_t& id);
  void handle_exit();
  
  // Text document handlers
  void handle_did_open(const glz::raw_json& params);
  void handle_did_change(const glz::raw_json& params);
  void handle_did_close(const glz::raw_json& params);
  void handle_hover(const glz::json_t& id, const glz::raw_json& params);
  void handle_definition(const glz::json_t& id, const glz::raw_json& params);
  void handle_document_symbol(const glz::json_t& id, const glz::raw_json& params);
  void handle_semantic_tokens_full(const glz::json_t& id, const glz::raw_json& params);
  
  // Debug commands
  void handle_debug_positions(const glz::json_t& id, const glz::raw_json& params);
  
  // Helper methods for hover
  std::string create_hover_content(const npidl::PositionIndex::Entry* entry);
  std::string format_type(npidl::AstTypeDecl* type);
  
  // Diagnostics
  void publish_diagnostics(const std::string& uri, const std::vector<lsp::Diagnostic>& diagnostics);
};

