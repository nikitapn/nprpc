// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>

namespace npidl {

// Forward declarations
struct AstNodeWithPosition;

// Position-based index for fast AST node lookup
// Built once after parsing, used for all LSP position queries (hover, definition, etc.)
class PositionIndex {
public:
    enum class NodeType {
        Interface,
        Struct,
        Exception,
        Enum,
        Function,
        Field,
        Parameter,
        Alias,
        Import,
        EnumValue
    };

    struct Entry {
        uint32_t start_line;
        uint32_t start_col;
        uint32_t end_line;
        uint32_t end_col;
        void* node;              // Pointer to AST node (type-erased)
        NodeType node_type;
        
        // Check if this entry contains the given position
        bool contains(uint32_t line, uint32_t col) const {
            if (line < start_line || line > end_line) return false;
            if (line == start_line && col < start_col) return false;
            if (line == end_line && col > end_col) return false;
            return true;
        }
        
        // Size of the range (smaller = more specific/nested)
        uint32_t size() const {
            return (end_line - start_line) * 10000 + (end_col - start_col);
        }
    };

private:
    std::vector<Entry> entries_;
    bool finalized_ = false;

public:
    PositionIndex() = default;
    
    // Add an entry to the index (before finalization)
    void add(void* node, NodeType type, uint32_t start_line, uint32_t start_col, 
             uint32_t end_line, uint32_t end_col) {
        entries_.push_back({start_line, start_col, end_line, end_col, node, type});
    }
    
    // Finalize the index (sorts entries for efficient lookup)
    // Must be called before using find_* methods
    void finalize() {
        // Sort by start position for efficient searching
        std::sort(entries_.begin(), entries_.end(), 
            [](const Entry& a, const Entry& b) {
                if (a.start_line != b.start_line)
                    return a.start_line < b.start_line;
                return a.start_col < b.start_col;
            });
        finalized_ = true;
    }
    
    // Find the most specific (smallest) entry at the given position
    // Returns nullptr if no entry contains the position
    const Entry* find_at_position(uint32_t line, uint32_t col) const {
        if (!finalized_) return nullptr;
        
        const Entry* best_match = nullptr;
        uint32_t smallest_size = UINT32_MAX;
        
        // Find all entries containing this position
        // Return the smallest (most specific/nested)
        for (const auto& entry : entries_) {
            if (entry.contains(line, col)) {
                uint32_t size = entry.size();
                if (size < smallest_size) {
                    smallest_size = size;
                    best_match = &entry;
                }
            }
        }
        
        return best_match;
    }
    
    // Find all entries at the given position, sorted by specificity (smallest first)
    // Useful for hierarchical queries (e.g., "function in interface in namespace")
    std::vector<const Entry*> find_all_at_position(uint32_t line, uint32_t col) const {
        std::vector<const Entry*> result;
        
        for (const auto& entry : entries_) {
            if (entry.contains(line, col)) {
                result.push_back(&entry);
            }
        }
        
        // Sort by size (most specific first)
        std::sort(result.begin(), result.end(),
            [](const Entry* a, const Entry* b) { 
                return a->size() < b->size(); 
            });
        
        return result;
    }
    
    // Clear the index
    void clear() {
        entries_.clear();
        finalized_ = false;
    }
    
    // Get all entries (for debugging/testing)
    const std::vector<Entry>& entries() const {
        return entries_;
    }
    
    bool is_finalized() const {
        return finalized_;
    }
    
    size_t size() const {
        return entries_.size();
    }
};

} // namespace npidl
