# Semantic Tokens Implementation for NPIDL LSP

## Overview
Implemented LSP Semantic Tokens support for AST-based syntax highlighting in the NPIDL language server. This provides more accurate and context-aware syntax coloring compared to regex-based font-lock highlighting.

## Implementation Details

### Protocol Structures (lsp_server.hpp)
Added the following LSP protocol structures:

1. **SemanticTokensParams** - Request parameters containing the document URI
2. **SemanticTokensResponse** - Response with encoded token array
3. **ServerCapabilities::SemanticTokensLegend** - Token type and modifier definitions
4. **ServerCapabilities::SemanticTokensOptions** - Capability configuration

### Token Legend
Defined 9 token types mapped to standard LSP semantic token types:
- `namespace` (0) - module declarations
- `interface` (1) - interface declarations  
- `class` (2) - struct and exception declarations
- `enum` (3) - enum declarations
- `function` (4) - function declarations
- `parameter` (5) - parameter declarations
- `property` (6) - field declarations
- `type` (7) - type references
- `keyword` (8) - language keywords (import, etc.)

Token modifiers:
- `readonly` (bit 0) - const values
- `declaration` (bit 1) - definition vs usage
- `deprecated` (bit 2) - deprecated items

### Handler Implementation (lsp_server.cpp)

**handle_initialize**: Populates `semanticTokensProvider` in server capabilities with the legend and full document support.

**handle_semantic_tokens_full**: 
1. Parses the textDocument URI from request params
2. Finds the project and gets its PositionIndex
3. Walks all indexed AST entries
4. Maps each entry's NodeType to a semantic token type
5. Encodes tokens using delta encoding (relative to previous token)
6. Returns array of uint32_t values: [deltaLine, deltaCol, length, tokenType, modifiers]

### Delta Encoding
Each token is encoded as 5 uint32_t values:
- **deltaLine**: Line offset from previous token (or absolute if first token)
- **deltaCol**: Column offset (from previous if same line, else absolute)
- **length**: Token length in characters
- **tokenType**: Index into token types legend
- **tokenModifiers**: Bitflags for modifiers

### Integration Points

1. **Request Dispatcher**: Added `textDocument/semanticTokens/full` to request router
2. **Glaze Metadata**: Added serialization metadata for all new structures
3. **PositionIndex**: Reused existing sorted AST index for efficient token generation

## Testing

Created test scripts:
- **test_semantic_tokens.py**: End-to-end LSP protocol test
- **decode_tokens.py**: Decodes and visualizes token data

Verified correct token generation for:
- Exception declarations (mapped to "class" type)
- Field declarations (mapped to "property" type) 
- Correct position encoding with delta values
- Correct "declaration" modifier on all indexed nodes

## Benefits

1. **Accurate Highlighting**: Based on actual AST parse tree, not regex patterns
2. **Context-Aware**: Distinguishes between struct, interface, enum, exception
3. **Extensible**: Easy to add more token types as language evolves
4. **Performance**: O(n) generation using pre-built sorted index

## Client Support

The Emacs lsp-mode client automatically uses semantic tokens when the server advertises the capability. No changes needed to npidl-mode.el beyond existing lsp-mode integration.

## Future Enhancements

Potential improvements:
1. Support for `textDocument/semanticTokens/range` (partial document)
2. Index type references (not just declarations) 
3. Add more specific modifiers (async, trusted, etc.)
4. Multi-line token support (currently skipped)
5. Incremental updates via `semanticTokens/full/delta`
