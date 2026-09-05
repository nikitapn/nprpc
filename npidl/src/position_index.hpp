// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace npidl {

// Forward declarations
struct AstNodeWithPosition;

// Position-based index for fast AST node lookup
// Built once after parsing, used for all LSP position queries (hover,
// definition, etc.)
class PositionIndex
{
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
    EnumValue,
    Optional,
    Keyword
  };

  struct Entry {
    uint32_t start_line;
    uint32_t start_col;
    uint32_t end_line;
    uint32_t end_col;
    void* node; // Pointer to AST node (type-erased)
    NodeType node_type;
    bool declaration = true;

    // Check if this entry contains the given position
    bool contains(uint32_t line, uint32_t col) const
    {
      if (line < start_line || line > end_line)
        return false;
      if (line == start_line && col < start_col)
        return false;
      if (line == end_line && col > end_col)
        return false;
      return true;
    }

    // Size of the range (smaller = more specific/nested)
    uint32_t size() const
    {
      return (end_line - start_line) * 10000 + (end_col - start_col);
    }
  };

private:
  std::vector<Entry> entries_;
  bool finalized_ = false;

public:
  PositionIndex() = default;

  // Add an entry to the index (before finalization)
  void add(void* node,
           NodeType type,
           uint32_t start_line,
           uint32_t start_col,
           uint32_t end_line,
           uint32_t end_col,
           bool declaration = true)
  {
    entries_.push_back(
        {start_line, start_col, end_line, end_col, node, type, declaration});
  }

  // Finalize the index (sorts entries for efficient lookup)
  // Must be called before using find_* methods
  void finalize()
  {
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
  const Entry* find_at_position(uint32_t line, uint32_t col) const
  {
    if (!finalized_)
      return nullptr;

    const Entry* best_match = nullptr;
    uint32_t smallest_size = UINT32_MAX;
    int best_rank = INT32_MAX;

    // Find all entries containing this position
    // Return the smallest (most specific/nested). Equal spans prefer a
    // named type (enum/struct/...) over Alias, which is also the fallback
    // tag for fundamentals.
    for (const auto& entry : entries_) {
      if (entry.contains(line, col)) {
        uint32_t size = entry.size();
        int rank = specificity_rank(entry.node_type);
        if (size < smallest_size ||
            (size == smallest_size && rank < best_rank)) {
          smallest_size = size;
          best_rank = rank;
          best_match = &entry;
        }
      }
    }

    return best_match;
  }

  // Find all entries at the given position, sorted by specificity (smallest
  // first) Useful for hierarchical queries (e.g., "function in interface in
  // namespace")
  std::vector<const Entry*> find_all_at_position(uint32_t line,
                                                 uint32_t col) const
  {
    std::vector<const Entry*> result;

    for (const auto& entry : entries_) {
      if (entry.contains(line, col)) {
        result.push_back(&entry);
      }
    }

    // Sort by size (most specific first), then named types over Alias.
    std::sort(result.begin(), result.end(), [](const Entry* a, const Entry* b) {
      if (a->size() != b->size())
        return a->size() < b->size();
      return specificity_rank(a->node_type) < specificity_rank(b->node_type);
    });

    return result;
  }

  // Clear the index
  void clear()
  {
    entries_.clear();
    finalized_ = false;
  }

  // Get all entries (for debugging/testing)
  const std::vector<Entry>& entries() const { return entries_; }

  bool is_finalized() const { return finalized_; }

  size_t size() const { return entries_.size(); }

  // Lower is more specific. Alias is last so a leftover fundamental tagged
  // as Alias does not win over the real enum/struct at the same span.
  static int specificity_rank(NodeType t)
  {
    switch (t) {
    case NodeType::Keyword:
    case NodeType::EnumValue:
      return 0;
    case NodeType::Parameter:
    case NodeType::Field:
    case NodeType::Function:
    case NodeType::Import:
      return 1;
    case NodeType::Enum:
    case NodeType::Struct:
    case NodeType::Exception:
    case NodeType::Interface:
      return 2;
    case NodeType::Optional:
      return 3;
    case NodeType::Alias:
      return 4;
    }
    return 5;
  }
};

} // namespace npidl
