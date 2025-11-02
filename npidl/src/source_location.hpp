// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <cstdint>
#include <string>

// Source position tracking for LSP support
struct SourcePosition {
	uint32_t line;
	uint32_t column;
	
	SourcePosition() : line(0), column(0) {}
	SourcePosition(uint32_t l, uint32_t c) : line(l), column(c) {}
	
	bool is_valid() const { return line > 0; }
};

struct SourceRange {
	SourcePosition start;
	SourcePosition end;
	
	SourceRange() = default;
	SourceRange(SourcePosition s, SourcePosition e) : start(s), end(e) {}
	SourceRange(uint32_t start_line, uint32_t start_col, uint32_t end_line, uint32_t end_col)
		: start(start_line, start_col), end(end_line, end_col) {}
	
	bool is_valid() const { return start.is_valid() && end.is_valid(); }
	bool contains(uint32_t line, uint32_t col) const {
		if (line < start.line || line > end.line) return false;
		if (line == start.line && col < start.column) return false;
		if (line == end.line && col > end.column) return false;
		return true;
	}
	bool contains(const SourcePosition& pos) const {
		return contains(pos.line, pos.column);
	}
};

// Mix-in class for AST nodes that need position tracking
struct AstNodeWithPosition {
	SourceRange range;
	std::string name; // For named nodes (interfaces, structs, functions, fields, etc.)
	
	void set_position(const SourceRange& r) { range = r; }
	void set_position(const SourcePosition& start, const SourcePosition& end) {
		range = SourceRange(start, end);
	}
	const SourceRange& get_range() const { return range; }
	
	// Helper to find if a position is within this node
	bool contains_position(uint32_t line, uint32_t column) const {
		return range.contains(line, column);
	}
};
