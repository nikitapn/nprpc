# Go-to-Definition Implementation for NPIDL LSP

## Overview
Implemented LSP go-to-definition support for the NPIDL language server. This enables users to navigate to type definitions by clicking on type references in fields, parameters, etc.

## Implementation Details

### Enhanced Position Index (position_index_builder.hpp)
Added comprehensive indexing of all AST node types:

1. **Added namespace type indexing** - `index_namespace_types()`
   - Walks the entire namespace tree recursively
   - Indexes all types stored in namespaces (aliases, enums, structs)
   
2. **Added `index_type()` method** to handle different type categories:
   - **Aliases** (`using` declarations) - NodeType::Alias
   - **Enums** - NodeType::Enum  
   - **Structs** - NodeType::Struct (non-exceptions)
   - Skips interfaces (indexed separately) and fundamental types (no position)

3. **Namespace API enhancement** (ast.hpp)
   - Added `types()` accessor to get all types in a namespace
   - Added `children()` accessor to get child namespaces
   - Enables traversal for indexing without breaking encapsulation

### Go-to-Definition Handler (lsp_server.cpp)

**handle_definition()** implements the following logic:

1. **Find node at cursor position** using PositionIndex
2. **For field/parameter nodes**:
   - Extract the type reference from AstFieldDecl
   - Unwrap container types (Optional, Vector, Array) to get base type
   - Get type's SourceRange from AstNodeWithPosition
   - Find the definition in the index by position
   - Return Location pointing to the definition

3. **For definition nodes** (Interface, Struct, Enum, etc.):
   - Already at the definition, return current position
   
4. **For fundamental types**:
   - Return null (no definition to jump to for `int32`, `string`, etc.)

### Position Resolution
Uses `SourceRange` from `AstNodeWithPosition` mixin:
- Enums have position set in `enum_decl()` via `set_node_position()`
- Aliases have position set similarly
- Structs, interfaces already had position tracking

### Type Unwrapping
Handles complex types correctly:
```cpp
// For field: data: vector<MyStruct>?
// Unwraps: Optional -> Vector -> MyStruct
// Jumps to: MyStruct definition
```

## Testing

Created test scripts:
- **test_goto_definition.py** - General definition tests
- **test_goto_alias.py** - Specific alias (`using oid_t = u64`) test

Verified correct navigation for:
- ✅ Type aliases (using declarations)
- ✅ Struct definitions
- ✅ Exception definitions
- ✅ Enum definitions
- ✅ Wrapped types (Optional, Vector, Array)
- ✅ Returns null for fundamental types (expected behavior)

## Example Usage

Given npidl code:
```npidl
using oid_t = u64;

ObjectId : flat {
  object_id: oid_t;  // <-- Click here
}
```

**Result**: Jumps to `using oid_t = u64;` on line 6

## Benefits

1. **Improved Navigation**: Quick jump to type definitions
2. **Code Understanding**: Easily explore type hierarchies
3. **Productivity**: Reduces manual searching through files
4. **Standards Compliance**: Full LSP go-to-definition support

## Future Enhancements

Potential improvements:
1. Cross-file navigation (imports)
2. Go-to-definition on type names in declarations
3. Find all references functionality
4. Peek definition (inline preview)

## Integration

Works seamlessly with Emacs lsp-mode and any LSP client that supports `textDocument/definition` requests.
