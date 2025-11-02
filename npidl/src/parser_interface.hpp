// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <vector>
#include <string>
#include <stdexcept>

// Forward declarations for Parser integration with LSP

class lexical_error : public std::runtime_error {
public:
	const int line;
	const int col;

	lexical_error(int _line, int _col, const char* msg)
		: std::runtime_error(msg), line(_line), col(_col) {}
	
	lexical_error(int _line, int _col, const std::string& msg)
		: std::runtime_error(msg), line(_line), col(_col) {}
};

class parser_error : public lexical_error {
public:
	parser_error(int _line, int _col, const char* msg)
		: lexical_error(_line, _col, msg) {}

	parser_error(int _line, int _col, const std::string& msg)
		: lexical_error(_line, _col, msg) {}
};

// Minimal parser interface for LSP
class Context;
class BuildGroup;

class ParserInterface {
public:
	virtual ~ParserInterface() = default;
	virtual void parse() = 0;
	virtual const std::vector<parser_error>& get_errors() const = 0;
	virtual bool has_errors() const = 0;
};

// Factory function (to be implemented in main.cpp)
ParserInterface* create_lsp_parser(const std::string& content, Context& ctx, BuildGroup& builder);
