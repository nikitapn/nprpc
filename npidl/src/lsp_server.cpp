// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "lsp_server.hpp"
#include "parse_for_lsp.hpp"
#include "position_index_builder.hpp"
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

std::vector<lsp::Diagnostic> DocumentManager::parse_and_get_diagnostics(
    npidl::WorkspaceManager& workspace,
    Document& doc
) {
  std::vector<lsp::Diagnostic> diagnostics;

  // Get or create project context for this file
  // We'll update imports after parsing, so pass empty vector for now
  auto* project = workspace.get_project_for_file(doc.uri, {});

  if (!project) {
    // Should never happen - workspace always creates a project
    lsp::Diagnostic diag;
    diag.range.start.line = 0;
    diag.range.start.character = 0;
    diag.range.end.line = 0;
    diag.range.end.character = 0;
    diag.severity = 1;  // Error
    diag.message = "Internal error: Failed to create project context";
    diag.source = "npidl";
    diagnostics.push_back(diag);
    return diagnostics;
  }

  // Parse into the project's persistent context
  std::vector<npidl::ParseError> parse_errors;
  npidl::parse_for_lsp(*project->ctx, doc.content, parse_errors);

  // Extract imports from parsed context
  doc.imports.clear();
  for (const auto* import : project->ctx->imports) {
    if (import->resolved) {
      doc.imports.push_back(import->resolved_path.string());
    }
  }

  // Update the project's import graph with the newly discovered imports
  project->add_file(doc.uri, doc.imports);

  // Build position index for fast LSP queries
  project->position_index.clear();
  npidl::PositionIndexBuilder index_builder(project->position_index, *project->ctx);
  index_builder.build();

  // Convert parse errors to LSP diagnostics
  for (const auto& err : parse_errors) {
    lsp::Diagnostic diag;
    
    // Split content into lines to find the error token
    std::istringstream stream(doc.content);
    std::string line;
    int current_line = 0;
    int start_char = err.col - 1;  // Convert 1-based to 0-based
    int end_char = err.col - 1;
    
    while (std::getline(stream, line)) {
      if (current_line == err.line - 1) {
        // Found the error line
        // The error position might be AFTER the problematic token
        // Search backwards to find the start of the token
        int pos = err.col - 1;  // 0-based position
        
        if (pos > 0 && pos <= static_cast<int>(line.size())) {
          // Skip backwards over whitespace
          while (pos > 0 && std::isspace(static_cast<unsigned char>(line[pos - 1]))) {
            pos--;
          }
          
          // Now find the start of the token (alphanumeric or underscore)
          int token_end = pos;
          while (pos > 0 && (std::isalnum(static_cast<unsigned char>(line[pos - 1])) || line[pos - 1] == '_')) {
            pos--;
          }
          
          // If we found a token, use it
          if (pos < token_end) {
            start_char = pos;
            end_char = token_end;
          } else {
            // No token found backwards, try forward from error position
            pos = err.col - 1;
            if (pos >= 0 && pos < static_cast<int>(line.size())) {
              int token_start = pos;
              while (pos < static_cast<int>(line.size()) && 
                     (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_')) {
                pos++;
              }
              if (pos > token_start) {
                start_char = token_start;
                end_char = pos;
              } else {
                // Fallback: highlight at least one character
                start_char = err.col - 1;
                end_char = std::min(err.col, static_cast<int>(line.size()));
              }
            }
          }
        }
        break;
      }
      current_line++;
    }
    
    // Convert from 1-based to 0-based indexing (LSP protocol requirement)
    diag.range.start.line = err.line - 1;
    diag.range.start.character = start_char;
    diag.range.end.line = err.line - 1;
    // Ensure end >= start
    diag.range.end.character = std::max(end_char, start_char + 1);
    diag.severity = 1;  // Error
    diag.message = err.message;
    diag.source = "npidl";
    diagnostics.push_back(diag);
  }

  return diagnostics;
}

// LspServer Implementation

LspServer::LspServer() {
  // Disable buffering on stdin/stdout for LSP protocol
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);
  std::cout.tie(nullptr);
  
  // Log to stderr (stdout is for LSP messages)
  std::clog << "NPIDL LSP Server starting..." << std::endl;
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
      std::clog << "Content-Length: " << content_length << std::endl;
    }
  }

  if (content_length == 0) {
    std::clog << "No content length found, exiting" << std::endl;
    return std::nullopt;
  }

  // Read content
  std::string content(content_length, '\0');
  std::cin.read(content.data(), content_length);

  if (!std::cin) {
    std::clog << "Failed to read " << content_length << " bytes from stdin" << std::endl;
    return std::nullopt;
  }

  std::clog << "Successfully read message of " << content_length << " bytes" << std::endl;
  return content;
}

void LspServer::send_message(const std::string& message) {
  std::cout << "Content-Length: " << message.size() << "\r\n\r\n" << message << std::flush;
}

void LspServer::send_response(const glz::json_t& id, const std::string& result) {
  jsonrpc::Response response;
  response.id = id;
  response.result = result;  // Only set result, not error

  std::string json = glz::write_json(response).value_or("{}");
  send_message(json);
}

void LspServer::send_error(const glz::json_t& id, int code, const std::string& message) {
  std::string error_obj = glz::write_json(glz::obj{"code", code, "message", message}).value_or("{}");

  jsonrpc::Response response;
  response.id = id;
  response.error = error_obj;  // Only set error, not result

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
  std::clog << "Handling initialize request" << std::endl;

  lsp::InitializeResult result;
  result.capabilities.textDocumentSync.openClose = 1;
  result.capabilities.textDocumentSync.change = 1; // Full sync for MVP
  result.capabilities.hoverProvider = true;
  result.capabilities.definitionProvider = true;
  result.capabilities.referencesProvider = true;
  result.capabilities.documentSymbolProvider = true;

  // Semantic tokens support
  lsp::ServerCapabilities::SemanticTokensOptions semanticTokens;
  semanticTokens.legend.tokenTypes = {
    "namespace",   // 0 - module
    "interface",   // 1 - interface
    "class",       // 2 - struct/exception
    "enum",        // 3 - enum
    "function",    // 4 - function
    "parameter",   // 5 - parameter
    "property",    // 6 - field
    "type",        // 7 - type reference
    "keyword"      // 8 - keywords (import, etc.)
  };
  semanticTokens.legend.tokenModifiers = {
    "readonly",    // 0 - const
    "declaration", // 1 - definition vs usage
    "deprecated"   // 2 - deprecated
  };
  semanticTokens.full = true;
  semanticTokens.range = false;
  result.capabilities.semanticTokensProvider = semanticTokens;

  std::string result_json = glz::write_json(result).value_or("{}");
  send_response(id, result_json);

  initialized_ = true;
}

void LspServer::handle_initialized(const glz::raw_json& params) {
  std::clog << "Client initialized notification received" << std::endl;
}

void LspServer::handle_shutdown(const glz::json_t& id) {
  std::clog << "Handling shutdown request" << std::endl;
  send_response(id, "null");
}

void LspServer::handle_exit() {
  std::clog << "Exiting LSP server" << std::endl;
  std::exit(0);
}

void LspServer::handle_did_open(const glz::raw_json& params) {
  lsp::DidOpenTextDocumentParams open_params;

  auto error = glz::read_json(open_params, params.str);
  if (error) {
    std::cerr << "Error parsing didOpen params: " << glz::format_error(error, params.str) << std::endl;
    return;
  }

  std::clog << "Document opened: " << open_params.textDocument.uri << std::endl;

  documents_.open(
    open_params.textDocument.uri,
    open_params.textDocument.text,
    open_params.textDocument.version
  );

  // Parse and send diagnostics
  if (auto* doc = documents_.get(open_params.textDocument.uri)) {
    auto diagnostics = documents_.parse_and_get_diagnostics(workspace_, *doc);
    publish_diagnostics(open_params.textDocument.uri, diagnostics);
  }
}

void LspServer::handle_did_change(const glz::raw_json& params) {
  lsp::DidChangeTextDocumentParams change_params;

  auto error = glz::read_json(change_params, params.str);
  if (error) {
    std::cerr << "Error parsing didChange params: " << glz::format_error(error, params.str) << '\n';
    return;
  }

  std::clog << "Document changed: " << change_params.textDocument.uri << std::endl;

  // Full document sync (change=1)
  if (!change_params.contentChanges.empty()) {
    documents_.change(
      change_params.textDocument.uri,
      change_params.contentChanges[0].text,
      change_params.textDocument.version
    );

    // Parse and send diagnostics
    if (auto* doc = documents_.get(change_params.textDocument.uri)) {
      auto diagnostics = documents_.parse_and_get_diagnostics(workspace_, *doc);
      publish_diagnostics(change_params.textDocument.uri, diagnostics);
    }
  }
}

void LspServer::handle_did_close(const glz::raw_json& params) {
  lsp::DidCloseTextDocumentParams close_params;

  auto error = glz::read_json(close_params, params.str);
  if (error) {
    std::cerr << "Error parsing didClose params: " << glz::format_error(error, params.str) << '\n';
    return;
  }

  std::clog << "Document closed: " << close_params.textDocument.uri << std::endl;

  // Remove from workspace (cleans up empty projects)
  workspace_.remove_file(close_params.textDocument.uri);

  // Remove from document manager
  documents_.close(close_params.textDocument.uri);
}

void LspServer::handle_hover(const glz::json_t& id, const glz::raw_json& params) {
  lsp::TextDocumentPositionParams pos_params;

  auto error = glz::read_json(pos_params, params.str);
  if (error) {
    std::cerr << "Error parsing hover params: " << glz::format_error(error, params.str) << '\n';
    send_response(id, "null");
    return;
  }

  std::clog << "Hover request at " << pos_params.textDocument.uri 
            << " line:" << pos_params.position.line 
            << " char:" << pos_params.position.character << std::endl;

  // Find the project containing this file
  auto* project = workspace_.find_project(pos_params.textDocument.uri);
  if (!project) {
    std::cerr << "No project found for " << pos_params.textDocument.uri << '\n';
    send_response(id, "null");
    return;
  }

  std::clog << "Project found. Index has " << project->position_index.size() << " entries" << '\n';
  std::clog << "Index finalized: " << (project->position_index.is_finalized() ? "yes" : "no") << '\n';

  // Convert from 0-based LSP position to 1-based parser position
  uint32_t line = pos_params.position.line + 1;
  uint32_t col = pos_params.position.character + 1;

  std::clog << "Looking for node at 1-based position " << line << ":" << col << std::endl;

  // Find the AST node at this position
  const auto* entry = project->position_index.find_at_position(line, col);
  if (!entry) {
    std::clog << "No AST node at position " << line << ":" << col << '\n';
    std::clog << "Dumping first 10 index entries:" << '\n';
    const auto& entries = project->position_index.entries();
    for (size_t i = 0; i < std::min(size_t(10), entries.size()); ++i) {
      std::clog << "  Entry " << i << ": lines " << entries[i].start_line << "-" << entries[i].end_line 
                << ", cols " << entries[i].start_col << "-" << entries[i].end_col << '\n';
    }
    send_response(id, "null");
    return;
  }

  std::clog << "Found node at " << entry->start_line << ":" << entry->start_col 
            << " - " << entry->end_line << ":" << entry->end_col << std::endl;

  // Create hover content based on node type
  std::string hover_markdown = create_hover_content(entry);

  lsp::Hover hover;
  hover.contents = hover_markdown;

  // Set range to the node's range
  lsp::Range range;
  range.start.line = entry->start_line - 1;  // Convert back to 0-based
  range.start.character = entry->start_col - 1;
  range.end.line = entry->end_line - 1;
  range.end.character = entry->end_col - 1;
  hover.range = range;

  std::string result = glz::write_json(hover).value_or("null");
  send_response(id, result);
}

std::string LspServer::create_hover_content(const npidl::PositionIndex::Entry* entry) {
  using NodeType = npidl::PositionIndex::NodeType;
  std::ostringstream md;

  switch (entry->node_type) {
    case NodeType::Interface: {
      auto* ifs = static_cast<npidl::AstInterfaceDecl*>(entry->node);
      md << "**interface** `" << ifs->name << "`\n\n";
      md << "Functions: " << ifs->fns.size() << "\n";
      if (!ifs->plist.empty()) {
        md << "Inherits from: ";
        for (size_t i = 0; i < ifs->plist.size(); ++i) {
          if (i > 0) md << ", ";
          md << "`" << ifs->plist[i]->name << "`";
        }
        md << "\n";
      }
      break;
    }

    case NodeType::Struct: {
      auto* s = static_cast<npidl::AstStructDecl*>(entry->node);
      md << "**struct** `" << s->name << "`\n\n";
      md << "Fields: " << s->fields.size() << "\n";
      if (s->size >= 0) {
        md << "Size: " << s->size << " bytes\n";
      }
      if (s->flat) {
        md << "Type: flat\n";
      }
      break;
    }

    case NodeType::Exception: {
      auto* ex = static_cast<npidl::AstStructDecl*>(entry->node);
      md << "**exception** `" << ex->name << "`\n\n";
      md << "Fields: " << ex->fields.size() << "\n";
      md << "Exception ID: " << ex->exception_id << "\n";
      break;
    }

    case NodeType::Enum: {
      auto* e = static_cast<npidl::AstEnumDecl*>(entry->node);
      md << "**enum** `" << e->name << "`\n\n";
      md << "Values: " << e->items.size() << "\n";
      break;
    }

    case NodeType::Function: {
      auto* fn = static_cast<npidl::AstFunctionDecl*>(entry->node);
      md << "**function** `" << fn->name << "`\n\n";
      md << "```npidl\n";
      if (fn->is_async) md << "async ";

      // Return type
      if (fn->ret_value->id == npidl::FieldType::Void) {
        md << "void";
      } else {
        md << format_type(fn->ret_value);
      }

      md << " " << fn->name << "(";

      // Parameters
      for (size_t i = 0; i < fn->args.size(); ++i) {
        if (i > 0) md << ", ";
        auto* arg = fn->args[i];
        md << (arg->modifier == npidl::ArgumentModifier::In ? "in " : "out ");
        md << format_type(arg->type) << " " << arg->name;
      }

      md << ")";

      if (fn->ex) {
        md << " raises(" << fn->ex->name << ")";
      }

      md << "\n```\n";
      break;
    }

    case NodeType::Field: {
      auto* field = static_cast<npidl::AstFieldDecl*>(entry->node);
      md << "**field** `" << field->name << "`\n\n";
      md << "Type: `" << format_type(field->type) << "`\n";
      break;
    }

    case NodeType::Parameter: {
      auto* param = static_cast<npidl::AstFunctionArgument*>(entry->node);
      md << "**parameter** `" << param->name << "`\n\n";
      md << "Type: `" << format_type(param->type) << "`\n";
      md << "Direction: " << (param->modifier == npidl::ArgumentModifier::In ? "in" : "out") << "\n";
      break;
    }

    case NodeType::Import: {
      auto* import = static_cast<npidl::AstImportDecl*>(entry->node);
      md << "📄 **import**\n\n";
      if (import->resolved) {
        md << "Path: `" << import->resolved_path.string() << "`\n";
        md << "Status: ✓ Resolved\n";
      } else {
        md << "Path: `" << import->import_path << "`\n";
        md << "Status: ❌ Not resolved\n";
        if (!import->error_message.empty()) {
          md << "Error: " << import->error_message << "\n";
        }
      }
      break;
    }

    case NodeType::Optional: {
      auto* opt = static_cast<npidl::AstTypeDecl*>(entry->node);
      md << "**optional type**\n\n";
      md << "Base type: `" << format_type(npidl::copt(opt)->type) << "`\n";
      break;
    }

    default:
      md << "Unknown node type\n";
      break;
  }

  return md.str();
}

std::string LspServer::format_type(npidl::AstTypeDecl* type) {
  using FieldType = npidl::FieldType;

  switch (type->id) {
    case FieldType::Fundamental: {
      auto* ft = npidl::cft(type);
      switch (ft->token_id) {
        case npidl::TokenId::Boolean: return "boolean";
        case npidl::TokenId::Int8:    return "i8";
        case npidl::TokenId::UInt8:   return "u8";
        case npidl::TokenId::Int16:   return "i16";
        case npidl::TokenId::UInt16:  return "u16";
        case npidl::TokenId::Int32:   return "i32";
        case npidl::TokenId::UInt32:  return "u32";
        case npidl::TokenId::Int64:   return "i64";
        case npidl::TokenId::UInt64:  return "u64";
        case npidl::TokenId::Float32: return "f32";
        case npidl::TokenId::Float64: return "f64";
        default: return "?";
      }
    }
    case FieldType::String:  return "string";
    case FieldType::Void:    return "void";
    case FieldType::Object:  return "object";
    case FieldType::Array: {
      auto* arr = npidl::car(type);
      return format_type(arr->type) + "[" + std::to_string(arr->length) + "]";
    }
    case FieldType::Vector: {
      auto* vec = npidl::cvec(type);
      return "vector<" + format_type(vec->type) + ">";
    }
    case FieldType::Optional: {
      auto* opt = npidl::copt(type);
      return format_type(opt->type) + "?";
    }
    case FieldType::Struct: {
      auto* s = npidl::cflat(type);
      return s->name;
    }
    case FieldType::Interface: {
      auto* ifs = npidl::cifs(type);
      return ifs->name;
    }
    case FieldType::Enum: {
      auto* e = npidl::cenum(type);
      return e->name;
    }
    case FieldType::Alias: {
      auto* alias = npidl::calias(type);
      return alias->name;
    }
    default:
      return "?";
  }
}

void LspServer::handle_semantic_tokens_full(const glz::json_t& id, const glz::raw_json& params) {
  lsp::SemanticTokensParams token_params;

  auto error = glz::read_json(token_params, params.str);
  if (error) {
    std::cerr << "Error parsing semantic tokens params: " << glz::format_error(error, params.str) << '\n';
    send_response(id, "null");
    return;
  }

  std::clog << "Semantic tokens request for " << token_params.textDocument.uri << std::endl;

  // Find the project containing this file
  auto* project = workspace_.find_project(token_params.textDocument.uri);
  if (!project) {
    std::cerr << "No project found for " << token_params.textDocument.uri << '\n';
    send_response(id, "null");
    return;
  }

  std::clog << "Generating semantic tokens. Index has " << project->position_index.size() << " entries" << std::endl;

  // Build semantic tokens array
  lsp::SemanticTokensResponse response;
  std::vector<uint32_t>& data = response.data;

  // Token types (must match legend in handle_initialize)
  enum TokenType {
    TT_NAMESPACE = 0,   // module
    TT_INTERFACE = 1,   // interface
    TT_CLASS = 2,       // struct/exception
    TT_ENUM = 3,        // enum
    TT_FUNCTION = 4,    // function
    TT_PARAMETER = 5,   // parameter
    TT_PROPERTY = 6,    // field
    TT_TYPE = 7,        // type reference
    TT_KEYWORD = 8      // keywords
  };

  // Token modifiers (bitflags)
  enum TokenModifier {
    TM_READONLY = 1 << 0,      // const
    TM_DECLARATION = 1 << 1,   // definition vs usage
    TM_DEPRECATED = 1 << 2     // deprecated
  };

  // Get all index entries and sort by position
  const auto& entries = project->position_index.entries();

  // Track previous token position for delta encoding
  uint32_t prev_line = 0;
  uint32_t prev_col = 0;

  for (const auto& entry : entries) {
    uint32_t token_type = TT_KEYWORD;
    uint32_t token_modifiers = TM_DECLARATION; // All indexed entries are declarations

    // Map node type to token type
    switch (entry.node_type) {
      case npidl::PositionIndex::NodeType::Interface:
        token_type = TT_INTERFACE;
        break;
      case npidl::PositionIndex::NodeType::Struct:
      case npidl::PositionIndex::NodeType::Exception:
        token_type = TT_CLASS;
        break;
      case npidl::PositionIndex::NodeType::Enum:
        token_type = TT_ENUM;
        break;
      case npidl::PositionIndex::NodeType::Function:
        token_type = TT_FUNCTION;
        break;
      case npidl::PositionIndex::NodeType::Field:
        token_type = TT_PROPERTY;
        break;
      case npidl::PositionIndex::NodeType::Parameter:
        token_type = TT_PARAMETER;
        break;
      case npidl::PositionIndex::NodeType::Import:
        token_type = TT_KEYWORD;
        break;
      case npidl::PositionIndex::NodeType::Alias:
        token_type = TT_TYPE;
        break;
      case npidl::PositionIndex::NodeType::EnumValue:
        token_type = TT_PROPERTY; // Treat enum values as properties
        break;
      case npidl::PositionIndex::NodeType::Optional:
        token_type = TT_TYPE;
        break;
    }

    // Convert from 1-based parser position to 0-based LSP position
    uint32_t line = entry.start_line - 1;
    uint32_t col = entry.start_col - 1;

    // Calculate token length (simplified - single line tokens)
    // Note: end_col is inclusive (points to last char), so we don't add 1
    uint32_t length = (entry.end_line == entry.start_line) 
                      ? (entry.end_col - entry.start_col + 1)
                      : 0; // Skip multi-line tokens for now

    if (length == 0) continue; // Skip invalid tokens

    // Delta encoding: each token is relative to previous
    uint32_t delta_line = (data.empty()) ? line : (line - prev_line);
    uint32_t delta_col = (delta_line == 0) ? (col - prev_col) : col;

    // Each token is 5 uint32_t values
    data.push_back(delta_line);
    data.push_back(delta_col);
    data.push_back(length);
    data.push_back(token_type);
    data.push_back(token_modifiers);

    prev_line = line;
    prev_col = col;
  }

  std::clog << "Generated " << (data.size() / 5) << " semantic tokens" << std::endl;

  std::string result = glz::write_json(response).value_or("null");
  send_response(id, result);
}

void LspServer::handle_definition(const glz::json_t& id, const glz::raw_json& params) {
  lsp::TextDocumentPositionParams pos_params;

  auto error = glz::read_json(pos_params, params.str);
  if (error) {
    std::cerr << "Error parsing definition params: " << glz::format_error(error, params.str) << '\n';
    send_response(id, "null");
    return;
  }

  std::clog << "Definition request at " << pos_params.textDocument.uri
            << " line:" << pos_params.position.line 
            << " char:" << pos_params.position.character << std::endl;

  // Find the project containing this file
  auto* project = workspace_.find_project(pos_params.textDocument.uri);
  if (!project) {
    std::cerr << "No project found for " << pos_params.textDocument.uri << '\n';
    send_response(id, "null");
    return;
  }

  // Convert from 0-based LSP position to 1-based parser position
  uint32_t line = pos_params.position.line + 1;
  uint32_t col = pos_params.position.character + 1;

  std::clog << "Looking for definition at 1-based position " << line << ":" << col << std::endl;

  // Find the AST node at this position
  const auto* entry = project->position_index.find_at_position(line, col);
  if (!entry) {
    std::cerr << "No AST node at position " << line << ":" << col << '\n';
    send_response(id, "null");
    return;
  }

  std::clog << "Found node type: " << static_cast<int>(entry->node_type) << std::endl;

  // Helper to get position from a type that inherits from AstNodeWithPosition
  auto get_type_position = [](npidl::AstTypeDecl* type) -> const npidl::SourceRange* {
    using FieldType = npidl::FieldType;

    switch (type->id) {
      case FieldType::Struct: {
        auto* s = npidl::cflat(type);
        return &s->range;
      }
      case FieldType::Interface: {
        auto* ifs = npidl::cifs(type);
        return &ifs->range;
      }
      case FieldType::Enum: {
        auto* e = npidl::cenum(type);
        return &e->range;
      }
      case FieldType::Alias: {
        auto* alias = npidl::calias(type);
        return &alias->range;
      }
      default:
        return nullptr;
    }
  };

  // Check if we're on a type reference or a type definition
  // Type references are added to the index pointing to AstTypeDecl nodes
  // For type references, we want to navigate to the actual definition
  if (entry->node_type == npidl::PositionIndex::NodeType::Struct ||
      entry->node_type == npidl::PositionIndex::NodeType::Interface ||
      entry->node_type == npidl::PositionIndex::NodeType::Enum ||
      entry->node_type == npidl::PositionIndex::NodeType::Alias ||
      entry->node_type == npidl::PositionIndex::NodeType::Optional) {
    // This is a type - could be a reference or the actual definition
    // Get the type's position from the AST node
    auto* type = static_cast<npidl::AstTypeDecl*>(entry->node);
    if (type->id == npidl::FieldType::Optional) {
      // Unwrap optional to get to the base type
      auto* opt = static_cast<npidl::AstWrapType*>(type);
      type = opt->type;
    }

    const npidl::SourceRange* type_range = get_type_position(type);

    if (type_range && type_range->is_valid()) {
      // Check if the position we clicked on matches the type's definition position
      // If not, it's a type reference and we need to jump to the definition
      if (entry->start_line != type_range->start.line || 
          entry->start_col != type_range->start.column) {
        // This is a type reference, find the actual definition
        std::clog << "Found type reference, looking for definition at " 
                  << type_range->start.line << ":" << type_range->start.column << std::endl;
        
        const auto* def_entry = project->position_index.find_at_position(
          type_range->start.line, type_range->start.column);
        
        if (def_entry) {
          lsp::Location location;
          location.uri = pos_params.textDocument.uri;
          location.range.start.line = def_entry->start_line - 1;
          location.range.start.character = def_entry->start_col - 1;
          location.range.end.line = def_entry->end_line - 1;
          location.range.end.character = def_entry->end_col - 1;

          std::string result = glz::write_json(location).value_or("null");
          send_response(id, result);
          return;
        }
      } else {
        // This is the actual definition, return its position
        std::clog << "Node is the type definition itself" << std::endl;

        lsp::Location location;
        location.uri = pos_params.textDocument.uri;
        location.range.start.line = entry->start_line - 1;
        location.range.start.character = entry->start_col - 1;
        location.range.end.line = entry->end_line - 1;
        location.range.end.character = entry->end_col - 1;

        std::string result = glz::write_json(location).value_or("null");
        send_response(id, result);
        return;
      }
    }
  }

  // For fields and parameters, try to find the type definition
  npidl::AstTypeDecl* type_to_find = nullptr;

  if (entry->node_type == npidl::PositionIndex::NodeType::Field ||
      entry->node_type == npidl::PositionIndex::NodeType::Parameter) {
    auto* field = static_cast<npidl::AstFieldDecl*>(entry->node);
    if (field && field->type) {
      type_to_find = field->type;
      std::clog << "Field/parameter with type id: " << static_cast<int>(type_to_find->id) << std::endl;
    }
  } else {
    // For other node types (functions, imports, etc.), they ARE the definition
    std::clog << "Node is a definition itself" << std::endl;

    lsp::Location location;
    location.uri = pos_params.textDocument.uri;
    location.range.start.line = entry->start_line - 1;
    location.range.start.character = entry->start_col - 1;
    location.range.end.line = entry->end_line - 1;
    location.range.end.character = entry->end_col - 1;

    std::string result = glz::write_json(location).value_or("null");
    send_response(id, result);
    return;
  }

  // If we have a type to find, get its position
  if (type_to_find) {
    // Unwrap optionals, vectors, and arrays to get to the base type
    npidl::AstTypeDecl* base_type = type_to_find;
    while (base_type->id == npidl::FieldType::Optional ||
           base_type->id == npidl::FieldType::Vector ||
           base_type->id == npidl::FieldType::Array) {
      auto* wrap = static_cast<npidl::AstWrapType*>(base_type);
      base_type = wrap->type;
    }

    const npidl::SourceRange* type_range = get_type_position(base_type);

    if (type_range && type_range->is_valid()) {
      std::clog << "Type definition position: " << type_range->start.line << ":" << type_range->start.column << std::endl;

      // Find the index entry for this type definition
      const auto* def_entry = project->position_index.find_at_position(
        type_range->start.line, type_range->start.column);

      if (def_entry) {
        lsp::Location location;
        location.uri = pos_params.textDocument.uri; // Same file for now (TODO: handle imports)
        location.range.start.line = def_entry->start_line - 1;
        location.range.start.character = def_entry->start_col - 1;
        location.range.end.line = def_entry->end_line - 1;
        location.range.end.character = def_entry->end_col - 1;

        std::string result = glz::write_json(location).value_or("null");
        send_response(id, result);
        return;
      }
    } else {
      std::clog << "Type is a fundamental type (no definition to jump to)" << std::endl;
    }
  }

  std::clog << "Could not resolve definition" << std::endl;
  send_response(id, "null");
}

void LspServer::handle_document_symbol(const glz::json_t& id, const glz::raw_json& params) {
  lsp::DocumentSymbolParams symbol_params;

  auto error = glz::read_json(symbol_params, params.str);
  if (error) {
    std::cerr << "Error parsing documentSymbol params: " << glz::format_error(error, params.str) << '\n';
    send_error(id, -32602, "Invalid params");
    return;
  }

  auto* project = workspace_.find_project(symbol_params.textDocument.uri);
  if (!project || !project->ctx) {
    send_response(id, "[]");
    return;
  }

  std::vector<lsp::DocumentSymbol> symbols;

  // Helper to convert SourceRange to LSP Range
  auto to_lsp_range = [](const npidl::SourceRange& sr) -> lsp::Range {
    lsp::Range range;
    range.start.line = sr.start.line - 1;
    range.start.character = sr.start.column - 1;
    range.end.line = sr.end.line - 1;
    range.end.character = sr.end.column - 1;
    return range;
  };

  // Traverse namespace tree and collect all types
  std::function<void(npidl::Namespace*)> visit_namespace = [&](npidl::Namespace* ns) {
    for (const auto& [type_name, type_decl] : ns->types()) {
      // Handle structs
      if (type_decl->id == npidl::FieldType::Struct) {
        auto* s = npidl::cflat(type_decl);
        lsp::DocumentSymbol symbol;
        symbol.name = s->name;
        symbol.kind = lsp::SymbolKind::Struct;
        symbol.range = to_lsp_range(s->range);
        symbol.selectionRange = symbol.range;
        
        // Add fields as children
        for (const auto& field : s->fields) {
          lsp::DocumentSymbol field_symbol;
          field_symbol.name = field->name;
          field_symbol.kind = lsp::SymbolKind::Field;
          if (field->range.is_valid()) {
            field_symbol.range = to_lsp_range(field->range);
            field_symbol.selectionRange = field_symbol.range;
          } else {
            field_symbol.range = symbol.range;
            field_symbol.selectionRange = symbol.range;
          }
          symbol.children.push_back(std::move(field_symbol));
        }
        
        symbols.push_back(std::move(symbol));
      }
      // Handle interfaces
      else if (type_decl->id == npidl::FieldType::Interface) {
        auto* iface = npidl::cifs(type_decl);
        lsp::DocumentSymbol symbol;
        symbol.name = iface->name;
        symbol.kind = lsp::SymbolKind::Interface;
        symbol.range = to_lsp_range(iface->range);
        symbol.selectionRange = symbol.range;
        
        // Add functions as children
        for (const auto& func : iface->fns) {
          lsp::DocumentSymbol func_symbol;
          func_symbol.name = func->name;
          func_symbol.kind = lsp::SymbolKind::Method;
          if (func->range.is_valid()) {
            func_symbol.range = to_lsp_range(func->range);
            func_symbol.selectionRange = func_symbol.range;
          } else {
            func_symbol.range = symbol.range;
            func_symbol.selectionRange = symbol.range;
          }
          symbol.children.push_back(std::move(func_symbol));
        }
        
        symbols.push_back(std::move(symbol));
      }
      // Handle enums
      else if (type_decl->id == npidl::FieldType::Enum) {
        auto* e = npidl::cenum(type_decl);
        lsp::DocumentSymbol symbol;
        symbol.name = e->name;
        symbol.kind = lsp::SymbolKind::Enum;
        symbol.range = to_lsp_range(e->range);
        symbol.selectionRange = symbol.range;
        
        // Add enum members
        for (const auto& [member_name, member_value] : e->items) {
          lsp::DocumentSymbol member_symbol;
          member_symbol.name = member_name;
          member_symbol.kind = lsp::SymbolKind::EnumMember;
          member_symbol.range = symbol.range; // Enums don't track member positions
          member_symbol.selectionRange = symbol.range;
          symbol.children.push_back(std::move(member_symbol));
        }
        
        symbols.push_back(std::move(symbol));
      }
      // Handle type aliases
      else if (type_decl->id == npidl::FieldType::Alias) {
        auto* alias = npidl::calias(type_decl);
        lsp::DocumentSymbol symbol;
        symbol.name = alias->name;
        symbol.kind = lsp::SymbolKind::TypeParameter;
        symbol.range = to_lsp_range(alias->range);
        symbol.selectionRange = symbol.range;
        symbols.push_back(std::move(symbol));
      }
    }

    // Recursively visit child namespaces
    for (auto* child : ns->children()) {
      visit_namespace(child);
    }
  };

  // Start from root namespace
  visit_namespace(project->ctx->nm_root());

  std::string result = glz::write_json(symbols).value_or("[]");
  send_response(id, result);
}

void LspServer::handle_debug_positions(const glz::json_t& id, const glz::raw_json& params) {
  lsp::TextDocumentIdentifier doc_id;
  
  auto error = glz::read_json(doc_id, params.str);
  if (error) {
    std::cerr << "Error parsing debug positions params: " << glz::format_error(error, params.str) << '\n';
    send_response(id, "null");
    return;
  }

  auto* project = workspace_.find_project(doc_id.uri);
  if (!project) {
    send_response(id, "\"No project found\"");
    return;
  }

  std::ostringstream result;
  result << "=== AST Position Index Debug ===\n";
  result << "Total entries: " << project->position_index.size() << "\n\n";

  const auto& entries = project->position_index.entries();
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    
    result << "Entry " << i << ":\n";
    result << "  Type: ";
    
    switch (entry.node_type) {
      case npidl::PositionIndex::NodeType::Interface:  result << "Interface"; break;
      case npidl::PositionIndex::NodeType::Struct:     result << "Struct"; break;
      case npidl::PositionIndex::NodeType::Exception:  result << "Exception"; break;
      case npidl::PositionIndex::NodeType::Enum:       result << "Enum"; break;
      case npidl::PositionIndex::NodeType::Function:   result << "Function"; break;
      case npidl::PositionIndex::NodeType::Field:      result << "Field"; break;
      case npidl::PositionIndex::NodeType::Parameter:  result << "Parameter"; break;
      case npidl::PositionIndex::NodeType::Import:     result << "Import"; break;
      case npidl::PositionIndex::NodeType::Alias:      result << "Alias"; break;
      case npidl::PositionIndex::NodeType::EnumValue:  result << "EnumValue"; break;
      default: result << "Unknown";
    }
    result << "\n";

    // Get the name if possible
    std::string name = "?";
    if (entry.node_type == npidl::PositionIndex::NodeType::Interface) {
      name = static_cast<npidl::AstInterfaceDecl*>(entry.node)->name;
    } else if (entry.node_type == npidl::PositionIndex::NodeType::Struct ||
               entry.node_type == npidl::PositionIndex::NodeType::Exception) {
      name = static_cast<npidl::AstStructDecl*>(entry.node)->name;
    } else if (entry.node_type == npidl::PositionIndex::NodeType::Enum) {
      name = static_cast<npidl::AstEnumDecl*>(entry.node)->name;
    } else if (entry.node_type == npidl::PositionIndex::NodeType::Function) {
      name = static_cast<npidl::AstFunctionDecl*>(entry.node)->name;
    } else if (entry.node_type == npidl::PositionIndex::NodeType::Field) {
      name = static_cast<npidl::AstFieldDecl*>(entry.node)->name;
    } else if (entry.node_type == npidl::PositionIndex::NodeType::Parameter) {
      name = static_cast<npidl::AstFunctionArgument*>(entry.node)->name;
    } else if (entry.node_type == npidl::PositionIndex::NodeType::Alias) {
      name = static_cast<npidl::AstAliasDecl*>(entry.node)->name;
    }

    result << "  Name: " << name << "\n";
    result << "  Position: " 
           << entry.start_line << ":" << entry.start_col 
           << " -> " 
           << entry.end_line << ":" << entry.end_col << "\n";
    
    // Calculate length for single-line tokens
    if (entry.start_line == entry.end_line) {
      uint32_t length = entry.end_col - entry.start_col;
      result << "  Length: " << length << " chars\n";
    }
    
    result << "\n";
  }

  std::string result_str = result.str();
  send_response(id, glz::write_json(result_str).value_or("null"));
}

void LspServer::publish_diagnostics(const std::string& uri, const std::vector<lsp::Diagnostic>& diagnostics) {
  std::string params = glz::write_json(glz::obj{
    "uri", uri,
    "diagnostics", diagnostics
  }).value_or("{}");

  send_notification("textDocument/publishDiagnostics", params);
}

void LspServer::run() {
  std::clog << "LSP Server ready, waiting for messages..." << std::endl;

  while (true) {
    auto message = read_message();
    if (!message) {
      std::cerr << "Failed to read message, exiting\n";
      break;
    }

    std::clog << "Received message: " << message->substr(0, 100) << "..." << std::endl;

    // Check if it's a request (has "id" field) or notification (no "id" field)
    bool has_id = message->find("\"id\"") != std::string::npos;

    if (has_id) {
      // It's a request
      jsonrpc::Request request;
      auto error = glz::read_json(request, *message);

      if (error) {
        std::cerr << "Failed to parse as request: " << glz::format_error(error, *message) << '\n';
        continue;
      }

      std::clog << "Request method: " << request.method << std::endl;

      if (request.method == "initialize") {
        handle_initialize(request.id, request.params);
      } else if (request.method == "shutdown") {
        handle_shutdown(request.id);
      } else if (request.method == "textDocument/hover") {
        handle_hover(request.id, request.params);
      } else if (request.method == "textDocument/definition") {
        handle_definition(request.id, request.params);
      } else if (request.method == "textDocument/documentSymbol") {
        handle_document_symbol(request.id, request.params);
      } else if (request.method == "textDocument/semanticTokens/full") {
        handle_semantic_tokens_full(request.id, request.params);
      } else if (request.method == "npidl/debugPositions") {
        handle_debug_positions(request.id, request.params);
      } else {
        std::cerr << "Unknown request method: " << request.method << '\n';
        send_error(request.id, -32601, "Method not found");
      }
    } else {
      // It's a notification
      jsonrpc::Notification notification;
      auto error = glz::read_json(notification, *message);

      if (error) {
        std::cerr << "Failed to parse as notification: " << glz::format_error(error, *message) << '\n';
        continue;
      }

      std::clog << "Notification method: " << notification.method << std::endl;

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
    }
  }
}
