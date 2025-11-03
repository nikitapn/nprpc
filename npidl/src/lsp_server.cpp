// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "lsp_server.hpp"
#include "parse_for_lsp.hpp"
#include <iostream>
#include <sstream>
#include <filesystem>

// DocumentManager Implementation

void DocumentManager::open(const std::string& uri, const std::string& text, int version) {
  Document doc;
  doc.uri = uri;
  doc.content = text;
  doc.version = version;
  documents_[uri] = std::move(doc);
}

void DocumentManager::change(const std::string& uri, const std::string& text, int version) {
  auto it = documents_.find(uri);
  if (it != documents_.end()) {
    it->second.content = text;
    it->second.version = version;
  }
}

void DocumentManager::close(const std::string& uri) {
  documents_.erase(uri);
}

DocumentManager::Document* DocumentManager::get(const std::string& uri) {
  auto it = documents_.find(uri);
  return it != documents_.end() ? &it->second : nullptr;
}

std::vector<lsp::Diagnostic> DocumentManager::parse_and_get_diagnostics(Document& doc) {
  std::vector<lsp::Diagnostic> diagnostics;

  std::vector<npidl::ParseError> parse_errors;
  npidl::parse_for_lsp(doc.content, parse_errors);

  // Convert parse errors to LSP diagnostics
  for (const auto& err : parse_errors) {
    lsp::Diagnostic diag;
    // Convert from 1-based to 0-based indexing (LSP protocol requirement)
    diag.range.start.line = err.line - 1;
    diag.range.start.character = err.col - 1;
    diag.range.end.line = err.line - 1;
    diag.range.end.character = err.col;  // Highlight one character
    diag.severity = 1;  // Error
    diag.message = err.message;
    diag.source = "npidl";
    diagnostics.push_back(diag);
  }

  return diagnostics;
}

// LspServer Implementation

LspServer::LspServer() {
  // Log to stderr (stdout is for LSP messages)
  std::cerr << "NPIDL LSP Server starting..." << std::endl;
}

std::optional<std::string> LspServer::read_message() {
  std::string header;
  int content_length = 0;

  // Read headers
  while (std::getline(std::cin, header)) {
    // Remove trailing \r if present (LSP uses \r\n line endings)
    if (!header.empty() && header.back() == '\r') {
      header.pop_back();
    }

    if (header.empty()) break;

    if (header.starts_with("Content-Length: ")) {
      content_length = std::stoi(header.substr(16));
    }
  }

  if (content_length == 0) {
    return std::nullopt;
  }

  // Read content
  std::string content(content_length, '\0');
  std::cin.read(content.data(), content_length);

  if (!std::cin) {
    return std::nullopt;
  }

  return content;
}

void LspServer::send_message(const std::string& message) {
  std::cout << "Content-Length: " << message.size() << "\r\n\r\n" << message << std::flush;
}

void LspServer::send_response(const glz::json_t& id, const std::string& result) {
  jsonrpc::Response response;
  response.id = id;
  response.result = result;

  std::string json = glz::write_json(response).value_or("{}");
  send_message(json);
}

void LspServer::send_error(const glz::json_t& id, int code, const std::string& message) {
  std::string error = glz::write_json(glz::obj{"code", code, "message", message}).value_or("{}");

  jsonrpc::Response response;
  response.id = id;
  response.error = error;

  std::string json = glz::write_json(response).value_or("{}");
  send_message(json);
}

void LspServer::send_notification(const std::string& method, const std::string& params) {
  jsonrpc::Notification notification;
  notification.method = method;
  notification.params = params;

  std::string json = glz::write_json(notification).value_or("{}");
  send_message(json);
}

void LspServer::handle_initialize(const glz::json_t& id, const glz::raw_json& params) {
  std::cerr << "Handling initialize request" << std::endl;

  lsp::InitializeResult result;
  result.capabilities.textDocumentSync.openClose = 1;
  result.capabilities.textDocumentSync.change = 1; // Full sync for MVP
  result.capabilities.hoverProvider = true;
  result.capabilities.definitionProvider = true;
  result.capabilities.referencesProvider = true;
  result.capabilities.documentSymbolProvider = true;

  std::string result_json = glz::write_json(result).value_or("{}");
  send_response(id, result_json);

  initialized_ = true;
}

void LspServer::handle_initialized(const glz::raw_json& params) {
  std::cerr << "Client initialized notification received" << std::endl;
}

void LspServer::handle_shutdown(const glz::json_t& id) {
  std::cerr << "Handling shutdown request" << std::endl;
  send_response(id, "null");
}

void LspServer::handle_exit() {
  std::cerr << "Exiting LSP server" << std::endl;
  std::exit(0);
}

void LspServer::handle_did_open(const glz::raw_json& params) {
  lsp::DidOpenTextDocumentParams open_params;

  auto error = glz::read_json(open_params, params.str);
  if (error) {
    std::cerr << "Error parsing didOpen params: " << glz::format_error(error, params.str) << std::endl;
    return;
  }

  std::cerr << "Document opened: " << open_params.textDocument.uri << std::endl;

  documents_.open(
    open_params.textDocument.uri,
    open_params.textDocument.text,
    open_params.textDocument.version
  );

  // Parse and send diagnostics
  if (auto* doc = documents_.get(open_params.textDocument.uri)) {
    auto diagnostics = documents_.parse_and_get_diagnostics(*doc);
    publish_diagnostics(open_params.textDocument.uri, diagnostics);
  }
}

void LspServer::handle_did_change(const glz::raw_json& params) {
  lsp::DidChangeTextDocumentParams change_params;

  auto error = glz::read_json(change_params, params.str);
  if (error) {
    std::cerr << "Error parsing didChange params: " << glz::format_error(error, params.str) << std::endl;
    return;
  }

  std::cerr << "Document changed: " << change_params.textDocument.uri << std::endl;

  // Full document sync (change=1)
  if (!change_params.contentChanges.empty()) {
    documents_.change(
      change_params.textDocument.uri,
      change_params.contentChanges[0].text,
      change_params.textDocument.version
    );

    // Parse and send diagnostics
    if (auto* doc = documents_.get(change_params.textDocument.uri)) {
      auto diagnostics = documents_.parse_and_get_diagnostics(*doc);
      publish_diagnostics(change_params.textDocument.uri, diagnostics);
    }
  }
}

void LspServer::handle_did_close(const glz::raw_json& params) {
  lsp::DidCloseTextDocumentParams close_params;

  auto error = glz::read_json(close_params, params.str);
  if (error) {
    std::cerr << "Error parsing didClose params: " << glz::format_error(error, params.str) << std::endl;
    return;
  }

  std::cerr << "Document closed: " << close_params.textDocument.uri << std::endl;
  documents_.close(close_params.textDocument.uri);
}

void LspServer::handle_hover(const glz::json_t& id, const glz::raw_json& params) {
  lsp::TextDocumentPositionParams pos_params;

  auto error = glz::read_json(pos_params, params.str);
  if (error) {
    std::cerr << "Error parsing hover params: " << glz::format_error(error, params.str) << std::endl;
    send_response(id, "null");
    return;
  }

  std::cerr << "Hover request at " << pos_params.textDocument.uri 
            << " line:" << pos_params.position.line 
            << " char:" << pos_params.position.character << std::endl;

  // TODO: Implement hover using AST
  // For MVP, return null
  send_response(id, "null");
}

void LspServer::handle_definition(const glz::json_t& id, const glz::raw_json& params) {
  lsp::TextDocumentPositionParams pos_params;

  auto error = glz::read_json(pos_params, params.str);
  if (error) {
    std::cerr << "Error parsing definition params: " << glz::format_error(error, params.str) << std::endl;
    send_response(id, "null");
    return;
  }

  std::cerr << "Definition request at " << pos_params.textDocument.uri 
            << " line:" << pos_params.position.line 
            << " char:" << pos_params.position.character << std::endl;

  // TODO: Implement go-to-definition using AST
  // For MVP, return null
  send_response(id, "null");
}

void LspServer::publish_diagnostics(const std::string& uri, const std::vector<lsp::Diagnostic>& diagnostics) {
  std::string params = glz::write_json(glz::obj{
    "uri", uri,
    "diagnostics", diagnostics
  }).value_or("{}");

  send_notification("textDocument/publishDiagnostics", params);
}

void LspServer::run() {
  std::cerr << "LSP Server ready, waiting for messages..." << std::endl;

  while (true) {
    auto message = read_message();
    if (!message) {
      std::cerr << "Failed to read message, exiting" << std::endl;
      break;
    }

    std::cerr << "Received message: " << message->substr(0, 100) << "..." << std::endl;

    // Try to parse as request first
    jsonrpc::Request request;
    auto error = glz::read_json(request, *message);

    if (!error) {
      // It's a request - dispatch based on method
      std::cerr << "Method: " << request.method << std::endl;

      if (request.method == "initialize") {
        handle_initialize(request.id, request.params);
      } else if (request.method == "shutdown") {
        handle_shutdown(request.id);
      } else if (request.method == "textDocument/hover") {
        handle_hover(request.id, request.params);
      } else if (request.method == "textDocument/definition") {
        handle_definition(request.id, request.params);
      } else {
        std::cerr << "Unknown request method: " << request.method << std::endl;
        send_error(request.id, -32601, "Method not found");
      }
      continue;
    }

    // Try to parse as notification
    jsonrpc::Notification notification;
    error = glz::read_json(notification, *message);

    if (!error) {
      std::cerr << "Notification: " << notification.method << std::endl;

      if (notification.method == "initialized") {
        handle_initialized(notification.params);
      } else if (notification.method == "exit") {
        handle_exit();
      } else if (notification.method == "textDocument/didOpen") {
        handle_did_open(notification.params);
      } else if (notification.method == "textDocument/didChange") {
        handle_did_change(notification.params);
      } else if (notification.method == "textDocument/didClose") {
        handle_did_close(notification.params);
      } else {
        std::cerr << "Unknown notification: " << notification.method << std::endl;
      }
      continue;
    }

    std::cerr << "Failed to parse message as request or notification" << std::endl;
  }
}
