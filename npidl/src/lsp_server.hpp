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

struct ServerCapabilities {
	struct TextDocumentSync {
		int openClose = 1;
		int change = 1; // 1=Full, 2=Incremental
	};
	
	TextDocumentSync textDocumentSync;
	bool hoverProvider = true;
	bool definitionProvider = true;
	bool referencesProvider = true;
	bool documentSymbolProvider = true;
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
struct glz::meta<lsp::ServerCapabilities::TextDocumentSync> {
	using T = lsp::ServerCapabilities::TextDocumentSync;
	static constexpr auto value = object(
		"openClose", &T::openClose,
		"change", &T::change
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
		"documentSymbolProvider", &T::documentSymbolProvider
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
	glz::raw_json result;
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
		std::unique_ptr<Context> context;
	};

	void open(const std::string& uri, const std::string& text, int version);
	void change(const std::string& uri, const std::string& text, int version);
	void close(const std::string& uri);
	
	Document* get(const std::string& uri);
	
	std::vector<lsp::Diagnostic> parse_and_get_diagnostics(Document& doc);

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
	
	// Diagnostics
	void publish_diagnostics(const std::string& uri, const std::vector<lsp::Diagnostic>& diagnostics);
};

