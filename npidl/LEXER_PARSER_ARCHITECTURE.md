# Lexer & Parser Architecture - Dual Mode Support

## Overview

The NPIDL parser now supports **two modes of operation**:

1. **File-Based Mode** (Compiler) - Reads from disk
2. **In-Memory Mode** (LSP) - Parses content from memory

This dual architecture enables:
- ✅ Traditional command-line compilation from files
- ✅ LSP server integration with unsaved editor content
- ✅ Future import resolution (resolved paths + in-memory content)

## Architecture Diagram

```
┌───────────────────────────────────────────────────────┐
│                     NPIDL Parser                      │
│                                                       │
│  ┌─────────────────────────────────────────────────┐  │
│  │              Lexer (Tokenization)               │  │
│  │                                                 │  │
│  │  Constructor 1: Lexer(Context&)           ,     │  │
│  │    - Reads file path from Context               │  │
│  │    - Opens file with std::ifstream              │  │
│  │    - Loads entire file into memory              │  │
│  │    - Use case: Compiler mode                    │  │
│  │                                                 │  │
│  │  Constructor 2: Lexer(string&, Context&)        │  │
│  │    - Takes content directly (no disk I/O)       │  │
│  │    - Use case: LSP mode, unit tests             │  │
│  │                                                 │  │
│  │  Tokenization: text_ → Token stream             │  │
│  └─────────────────────────────────────────────────┘  │
│                          │                            │
│                          ↓                            │
│  ┌─────────────────────────────────────────────────┐  │
│  │              Parser (AST Building)              │  │
│  │                                                 │  │
│  │  Constructor 1: Parser(Context&, BuildGroup&)   │  │
│  │    - Creates file-based Lexer                   │  │
│  │    - Throws on first error (fail-fast)          │  │
│  │    - Use case: Compiler mode                    │  │
│  │                                                 │  │
│  │  Constructor 2: Parser(string&, Context&,       │  │
│  │                        BuildGroup&, bool)       │  │
│  │    - Creates in-memory Lexer                    │  │
│  │    - Optional error recovery (LSP mode)         │  │
│  │    - Use case: LSP mode, unit tests             │  │
│  │                                                 │  │
│  │  Parsing: Token stream → AST                    │  │
│  └─────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────┘
```

## Code Examples

### Mode 1: File-Based Parsing (Compiler)

```cpp
// Traditional compilation from file
Context ctx;
ctx.open("/path/to/input.npidl");  // Opens file, initializes namespace

BuildGroup builder(&ctx);
builder.add<CppBuilder>();
builder.add<TypeScriptBuilder>();

Parser parser(ctx, builder);  // File-based constructor
parser.parse();               // Throws on first error

builder.finalize();           // Generate output files
```

**Flow:**
1. `Context::open()` stores file path
2. `Parser` creates `Lexer(ctx)`
3. `Lexer` reads file from disk via `ctx.get_file_path()`
4. Parse completes or throws exception
5. Output generated to disk

### Mode 2: In-Memory Parsing (LSP)

```cpp
// LSP: Parse unsaved editor content
std::string content = "interface Foo { void bar(); };";

Context ctx;
ctx.open("<in-memory>");  // Dummy path for error reporting

BuildGroup builder(&ctx);
builder.add<NullBuilder>();  // No code generation

Parser parser(content, ctx, builder, true);  // In-memory + recovery
parser.parse();  // Collects all errors, doesn't throw

// Get all errors for diagnostics
for (const auto& err : parser.get_errors()) {
    send_diagnostic(err.line, err.col, err.message);
}
```

**Flow:**
1. `Context::open()` with dummy path (for error reporting)
2. `Parser` creates `Lexer(content, ctx)` 
3. `Lexer` uses provided content (no disk I/O)
4. Parse with error recovery collects all errors
5. Return errors to LSP client

### Helper Function: parse_for_lsp()

Convenience function for LSP integration:

```cpp
// parse_for_lsp.hpp
namespace npidl {
    struct ParseError {
        int line, col;
        std::string message;
    };
    
    bool parse_for_lsp(const std::string& content, 
                       std::vector<ParseError>& errors);
}

// Usage
std::vector<npidl::ParseError> errors;
if (!npidl::parse_for_lsp(document_content, errors)) {
    // Has errors
    for (auto& err : errors) {
        show_error(err.line, err.col, err.message);
    }
}
```

## Lexer Implementation Details

### File-Based Constructor

```cpp
Lexer(Context& ctx) : text_(), ctx_(ctx) {
    auto file_path = ctx_.get_file_path();
    if (!std::filesystem::exists(file_path))
        throw std::runtime_error("cannot read file");
    
    std::ifstream ifs(file_path);
    std::noskipws(ifs);
    std::copy(std::istream_iterator<char>(ifs), 
              std::istream_iterator<char>(), 
              std::back_inserter(text_));
    text_ += '\0';
    ptr_ = text_.c_str();
}
```

**Key Points:**
- Reads entire file into `text_` buffer
- Validates file exists
- Throws exception if file not found
- Null-terminates for safe parsing

### In-Memory Constructor

```cpp
Lexer(const std::string& content, Context& ctx) 
    : text_(content), ctx_(ctx) {
    text_ += '\0';
    ptr_ = text_.c_str();
}
```

**Key Points:**
- Copies content string (no disk I/O)
- Null-terminates for consistency
- Context still available for error reporting
- Same tokenization logic as file mode

## Parser Implementation Details

### File-Based Constructor

```cpp
Parser(Context& ctx, BuildGroup& builder)
    : lex_(ctx)  // ← File-based Lexer
    , ctx_(ctx)
    , builder_(builder)
    , enable_recovery_(false)  // Fail-fast
{
}
```

**Behavior:**
- Creates `Lexer(ctx)` - reads from disk
- Error recovery disabled (throws on first error)
- Used for compilation where single error stops build

### In-Memory Constructor

```cpp
Parser(const std::string& content, Context& ctx, 
       BuildGroup& builder, bool enable_recovery = false)
    : lex_(content, ctx)  // ← In-memory Lexer
    , ctx_(ctx)
    , builder_(builder)
    , enable_recovery_(enable_recovery)
{
}
```

**Behavior:**
- Creates `Lexer(content, ctx)` - uses memory
- Error recovery optional (typically enabled for LSP)
- Collects all errors instead of throwing
- Synchronizes to safe points and continues

## Context Handling

### Context Initialization

The `Context` class tracks file information:

```cpp
class Context {
    std::filesystem::path file_path;
    std::string base_name;
    Namespace* nm_root_;
    Namespace* nm_cur_;
    
public:
    void open(std::filesystem::path path) {
        file_path = std::move(path);
        base_name = file_path.filename()
                             .replace_extension()
                             .string();
        nm_root_ = nm_cur_ = new Namespace();
    }
    
    std::string current_file_path() const noexcept {
        // Safe for non-existent files (LSP mode)
        std::error_code ec;
        auto canonical = std::filesystem::canonical(file_path, ec);
        if (!ec) return canonical.string();
        return std::filesystem::absolute(file_path).string();
    }
};
```

**Key Points:**
- `open()` must be called even for in-memory mode
- Use dummy path like `"<in-memory>"` for LSP
- `current_file_path()` safe for non-existent files
- Initializes namespace hierarchy

## Import System Integration

### How Imports Will Work

With this dual architecture, imports can:

1. **Resolve import paths** from Context file path
2. **Check DocumentManager** for open files (in-memory)
3. **Fall back to disk** for closed files

```cpp
// Import resolution example
class ImportResolver {
    DocumentManager& docs_;
    
    void resolve_import(const std::string& import_path, 
                       const Context& ctx) {
        // 1. Resolve relative to current file
        auto resolved = ctx.current_file_path()
                           .parent_path() / import_path;
        
        // 2. Check if file is open in editor
        auto uri = path_to_uri(resolved);
        if (auto* doc = docs_.get(uri)) {
            // Parse in-memory content
            Context import_ctx;
            import_ctx.open(resolved);
            Parser parser(doc->content, import_ctx, builder, true);
            parser.parse();
        } else {
            // Parse from disk
            Context import_ctx;
            import_ctx.open(resolved);
            Parser parser(import_ctx, builder);
            parser.parse();
        }
    }
};
```

### LSP Protocol Details

LSP clients provide:

```json
{
  "textDocument": {
    "uri": "file:///home/user/project/main.npidl",
    "languageId": "npidl",
    "version": 1,
    "text": "interface Foo { };"
  }
}
```

**Advantages:**
- ✅ Absolute file path in URI
- ✅ Full content (including unsaved changes)
- ✅ Easy relative import resolution
- ✅ No need to guess file locations

## Error Handling

### File Mode (Throws)

```cpp
Parser parser(ctx, builder);
try {
    parser.parse();
} catch (const parser_error& e) {
    std::cerr << e.file_path() << ":" 
              << e.line() << ":" 
              << e.col() << ": " 
              << e.what() << '\n';
    return 1;
}
```

### LSP Mode (Collects)

```cpp
Parser parser(content, ctx, builder, true);
parser.parse();  // Doesn't throw

for (const auto& err : parser.get_errors()) {
    lsp::Diagnostic diag;
    diag.range.start = {err.line - 1, err.col - 1};
    diag.message = err.message;
    diag.severity = 1;  // Error
    send_diagnostic(uri, diag);
}
```

## Testing

### Unit Tests Use In-Memory Mode

```cpp
TEST(Parser, InterfaceDeclaration) {
    std::string code = R"(
        namespace Test {
            interface Foo {
                void bar(i32 x);
            };
        }
    )";
    
    std::vector<npidl::ParseError> errors;
    bool success = npidl::parse_for_lsp(code, errors);
    
    ASSERT_TRUE(success);
    ASSERT_EQ(errors.size(), 0);
}
```

**Benefits:**
- No temporary files needed
- Fast test execution
- Easy setup/teardown
- Precise error location testing

## Migration Path

### Before (Single Mode)

```cpp
// Only file-based parsing
Lexer lex(ctx);  // Always reads from disk
Parser parser(ctx, builder);
```

**Limitations:**
- Can't parse unsaved editor content
- Can't handle in-memory strings
- Not suitable for LSP

### After (Dual Mode)

```cpp
// File-based
Parser parser(ctx, builder);

// In-memory
Parser parser(content, ctx, builder, recovery);
```

**Advantages:**
- ✅ Supports both use cases
- ✅ Minimal code duplication
- ✅ Same parsing logic
- ✅ Ready for imports

## Performance Considerations

### Memory

- **File mode**: One copy in Lexer buffer
- **In-memory mode**: One copy when constructing Lexer
- **Impact**: Minimal (same memory either way)

### Speed

- **File mode**: +I/O time (disk read)
- **In-memory mode**: No I/O (instant)
- **Parsing**: Identical speed

### Typical Sizes

| File Size | Parse Time | Memory |
|-----------|------------|--------|
| 1 KB | <1ms | ~2 KB |
| 10 KB | <5ms | ~20 KB |
| 100 KB | <50ms | ~200 KB |

## Summary

### What We Have

✅ **Dual-mode architecture**
- File-based for compiler
- In-memory for LSP

✅ **Two Lexer constructors**
- `Lexer(Context&)` - reads from disk
- `Lexer(const string&, Context&)` - uses memory

✅ **Two Parser constructors**
- `Parser(Context&, BuildGroup&)` - file mode
- `Parser(string&, Context&, BuildGroup&, bool)` - memory mode

✅ **Safe Context handling**
- Works with non-existent files (LSP)
- Proper error reporting
- Ready for import resolution

### What's Next

⏳ **Import system implementation**
- Add `import` keyword
- Path resolution logic
- Recursive parsing
- Symbol table merging

⏳ **LSP integration**
- Use in-memory mode for open documents
- Fallback to disk for imports
- Track dependency graph
- Incremental updates

### Key Takeaway

The parser now has a **clean separation** between:
- **Data source** (file vs memory) → Lexer level
- **Error handling** (throw vs collect) → Parser level
- **Output generation** (real vs null) → Builder level

This makes it easy to add new features like imports while maintaining backwards compatibility with the existing compiler.
