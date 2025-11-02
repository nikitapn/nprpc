# Position Tracking for LSP Support

## Overview

To support LSP features like diagnostics, hover, and go-to-definition, we need to track source code positions throughout the compilation process. This document explains the position tracking system added to the NPIDL compiler.

## Architecture

### 1. Source Position Structures (`source_location.hpp`)

```cpp
struct SourcePosition {
    uint32_t line;
    uint32_t column;
};

struct SourceRange {
    SourcePosition start;
    SourcePosition end;
};

struct AstNodeWithPosition {
    SourceRange range;
    std::string name;
};
```

These structures provide:
- Line/column tracking (1-indexed for LSP compatibility)
- Range checking (is a cursor position within this node?)
- Mix-in capability for AST nodes

### 2. Token Position Tracking (`main.cpp`)

**Before:**
```cpp
struct Token {
    TokenId id;
    std::string name;
};
```

**After:**
```cpp
struct Token {
    TokenId id;
    std::string name;
    int line;
    int col;
};
```

Every token now knows where it came from in the source file.

### 3. Lexer Updates

The lexer captures position at the start of each token:

```cpp
Token tok() {
    skip_wp();
    int tok_line = line_;  // Capture position BEFORE parsing
    int tok_col = col_;
    
    // ... parsing logic ...
    return Token(TokenId::Something, "value", tok_line, tok_col);
}
```

## Current Implementation Status

### ✅ Completed

1. **Token Positions** - All tokens track their source location
2. **Lexer Position Tracking** - Line/column maintained during scanning
3. **Position Structures** - Reusable types for ranges and positions

### 🚧 Next Steps

To make this useful for LSP, you need to:

#### Step 1: Add Position Tracking to AST Nodes

The most important AST nodes to track:

```cpp
// In ast.hpp

struct AstInterfaceDecl : AstNodeWithPosition {
    // existing fields...
    // Now has: SourceRange range, std::string name
};

struct AstStructDecl : AstNodeWithPosition {
    // existing fields...
};

struct AstFunctionDecl : AstNodeWithPosition {
    // existing fields...
};

struct AstFieldDecl : AstNodeWithPosition {
    // existing fields...
};
```

#### Step 2: Capture Positions During Parsing

In the Parser, when creating AST nodes:

```cpp
bool interface_decl(attributes_t& attr) {
    if (peek() != TokenId::Interface) return false;
    
    auto interface_token = peek();  // Has position!
    flush();

    auto ifs = new AstInterfaceDecl();
    ifs->name = match(TokenId::Identifier).name;
    
    // NEW: Capture start position
    ifs->range.start = SourcePosition(interface_token.line, interface_token.col);
    
    // ... parse interface body ...
    
    auto close_brace = match('}');
    // NEW: Capture end position
    ifs->range.end = SourcePosition(close_brace.line, close_brace.col);
    
    return true;
}
```

#### Step 3: Build a Symbol Table with Positions

Create a structure to query symbols by position:

```cpp
struct SymbolInfo {
    std::string name;
    SourceRange range;
    AstTypeDecl* type;
    std::string hover_text;  // For hover tooltips
    SourcePosition definition_pos;  // For go-to-definition
};

class SymbolTable {
    std::vector<SymbolInfo> symbols_;
    
public:
    void add_symbol(const SymbolInfo& info);
    SymbolInfo* find_at_position(uint32_t line, uint32_t col);
    std::vector<SymbolInfo*> find_references(const std::string& name);
};
```

#### Step 4: Integrate with DocumentManager

Update the DocumentManager to build and query the symbol table:

```cpp
class DocumentManager {
    // Add symbol table per document
    std::unordered_map<std::string, SymbolTable> symbol_tables_;
    
    std::vector<lsp::Diagnostic> parse_and_get_diagnostics(Document& doc) {
        try {
            Context ctx{"lsp_module"};
            // ... parse ...
            
            // NEW: Build symbol table from AST
            SymbolTable symbols;
            build_symbol_table(ctx, symbols);
            symbol_tables_[doc.uri] = std::move(symbols);
            
        } catch (parser_error& e) {
            // Create diagnostic with e.line, e.col
        }
    }
    
    SymbolInfo* find_symbol_at(const std::string& uri, uint32_t line, uint32_t col) {
        if (auto it = symbol_tables_.find(uri); it != symbol_tables_.end()) {
            return it->second.find_at_position(line, col);
        }
        return nullptr;
    }
};
```

## Implementation Plan

### Phase 1: AST Position Tracking (1-2 days)

1. Update `AstInterfaceDecl`, `AstStructDecl`, `AstFunctionDecl`, etc. to inherit from `AstNodeWithPosition`
2. Modify parser to capture start/end positions for each node
3. Test that positions are correct

**File to modify:** `ast.hpp`

**Key changes:**
```cpp
struct AstInterfaceDecl : AstNodeWithPosition {
    // Remove std::string name; (now in AstNodeWithPosition)
    // ... rest of fields
};
```

### Phase 2: Error Position Tracking (1 day)

Make parser errors store exact positions:

```cpp
std::vector<lsp::Diagnostic> parse_and_get_diagnostics(Document& doc) {
    std::vector<lsp::Diagnostic> diagnostics;
    
    try {
        Parser parser(doc.content, ctx, builder);
        parser.parse();
    } catch (lexical_error& e) {
        lsp::Diagnostic diag;
        diag.range.start = {e.line - 1, e.col - 1};  // LSP is 0-indexed
        diag.range.end = {e.line - 1, e.col};
        diag.severity = 1;  // Error
        diag.message = e.what();
        diagnostics.push_back(diag);
    }
    
    return diagnostics;
}
```

### Phase 3: Symbol Table (2-3 days)

Build a symbol table by walking the AST:

```cpp
void build_symbol_table(Context& ctx, SymbolTable& table) {
    // Walk all namespaces
    for (auto* nm : ctx.all_namespaces()) {
        // Add interfaces
        for (auto* ifs : nm->interfaces()) {
            SymbolInfo info;
            info.name = ifs->name;
            info.range = ifs->range;
            info.type = ifs;
            info.hover_text = format_interface_hover(ifs);
            table.add_symbol(info);
            
            // Add methods
            for (auto* fn : ifs->fns) {
                SymbolInfo fn_info;
                fn_info.name = fn->name;
                fn_info.range = fn->range;
                fn_info.hover_text = format_function_hover(fn);
                table.add_symbol(fn_info);
            }
        }
        
        // Similar for structs, enums, etc.
    }
}
```

### Phase 4: LSP Feature Implementation (2-3 days)

#### Hover

```cpp
void LspServer::handle_hover(const glz::json_t& id, const glz::raw_json& params) {
    lsp::TextDocumentPositionParams pos_params;
    glz::read_json(pos_params, params.str);
    
    auto* symbol = documents_.find_symbol_at(
        pos_params.textDocument.uri,
        pos_params.position.line + 1,  // Convert to 1-indexed
        pos_params.position.character + 1
    );
    
    if (symbol) {
        lsp::Hover hover;
        hover.contents.value = symbol->hover_text;
        hover.range = to_lsp_range(symbol->range);
        send_response(id, glz::write_json(hover).value_or("null"));
    } else {
        send_response(id, "null");
    }
}
```

#### Go-to-Definition

```cpp
void LspServer::handle_definition(const glz::json_t& id, const glz::raw_json& params) {
    lsp::TextDocumentPositionParams pos_params;
    glz::read_json(pos_params, params.str);
    
    auto* symbol = documents_.find_symbol_at(
        pos_params.textDocument.uri,
        pos_params.position.line + 1,
        pos_params.position.character + 1
    );
    
    if (symbol && symbol->definition_pos.is_valid()) {
        lsp::Location loc;
        loc.uri = pos_params.textDocument.uri;
        loc.range.start.line = symbol->definition_pos.line - 1;  // LSP is 0-indexed
        loc.range.start.character = symbol->definition_pos.column - 1;
        send_response(id, glz::write_json(loc).value_or("null"));
    } else {
        send_response(id, "null");
    }
}
```

## Alternative Approach: Full CST

If you want to preserve ALL information (whitespace, comments, exact formatting):

### Option A: Separate CST Pass

1. Build a full CST that preserves everything
2. Build AST from CST
3. Query CST for positions when needed

**Pros:** Complete information, can implement refactoring
**Cons:** More complex, slower, more memory

### Option B: Position-Annotated AST (Current Approach)

1. AST with position ranges
2. Symbol table for quick lookups
3. Re-parse on demand for detailed analysis

**Pros:** Simple, fast, less memory
**Cons:** Can't preserve exact formatting, limited refactoring

## Recommendation

**Start with Position-Annotated AST** (current approach):

1. ✅ Tokens have positions (done)
2. 🔄 Add positions to AST nodes (next)
3. 🔄 Build symbol table from AST
4. 🔄 Implement hover/definition using symbol table

This gives you 90% of LSP features with minimal changes to your existing architecture. You can always add a full CST later if needed.

## Testing Strategy

1. **Unit Test Positions:**
   ```cpp
   Token t = lexer.tok();
   assert(t.line == expected_line);
   assert(t.col == expected_col);
   ```

2. **Integration Test Symbol Lookup:**
   ```cpp
   // Parse: "interface Calculator { i32 add(i32 a, i32 b); };"
   auto* symbol = table.find_at_position(1, 15);  // Position of "Calculator"
   assert(symbol != nullptr);
   assert(symbol->name == "Calculator");
   ```

3. **LSP Test:**
   ```bash
   # Hover request at position of a symbol
   echo '{"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///test.npidl"},"position":{"line":0,"character":10}}}' | npidl --lsp
   ```

## Files to Modify

1. ✅ `source_location.hpp` - Position structures (created)
2. ✅ `main.cpp` - Token position tracking (done)
3. 🔄 `ast.hpp` - Add AstNodeWithPosition to declarations
4. 🔄 `main.cpp` - Capture positions during parsing
5. 🔄 `lsp_server.hpp` - Add SymbolTable class
6. 🔄 `lsp_server.cpp` - Implement symbol table building
7. 🔄 `lsp_server.cpp` - Implement hover/definition handlers

## Summary

You now have **token-level position tracking**. The next step is to propagate these positions into your AST nodes and build a symbol table. This will enable all LSP features without requiring a full CST rewrite.

The key insight: **You don't need perfect CST for good LSP support**. Position-annotated AST + symbol table is sufficient for diagnostics, hover, and go-to-definition.
