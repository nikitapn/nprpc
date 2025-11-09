# Import Context Stack Design

## Problem Statement

When implementing `import` statements, the parser needs to:
1. Parse the main file
2. Encounter an `import` statement
3. Switch to parsing the imported file
4. Complete the imported file
5. Return to parsing the main file from where it left off

This requires **stack-based file tracking** in the Context.

## Solution: File Context Stack

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│ Context                                                 │
│                                                         │
│  file_stack_: vector<FileContext>                       │
│  ┌─────────────────────────────────────────┐            │
│  │ [0] main.npidl    (nm: ::)              │ ← Bottom   │
│  │ [1] types.npidl   (nm: ::Common)        │            │
│  │ [2] utils.npidl   (nm: ::Common::Utils) │ ← Top      │
│  └─────────────────────────────────────────┘            │
│                                                         │
│  Current state:                                         │
│    file_path  = "utils.npidl"                           │
│    base_name  = "utils"                                 │
│    nm_cur_    = ::Common::Utils                         │
│                                                         │
│  After pop_file():                                      │
│    file_path  = "types.npidl"                           │
│    base_name  = "types"                                 │
│    nm_cur_    = ::Common                                │
└─────────────────────────────────────────────────────────┘
```

### Data Structure

```cpp
struct FileContext {
    std::filesystem::path file_path;      // Path to the file
    std::string base_name;                 // Base name for error reporting
    Namespace* namespace_at_entry;         // Namespace when entering file
};

std::vector<FileContext> file_stack_;  // Stack of file contexts
```

## API

### `push_file(new_file_path)`

**When to call:** Before parsing an imported file

**What it does:**
1. Saves current file context to stack
2. Switches to new file
3. **Preserves** namespace context (imports share namespace hierarchy)

```cpp
// Example: Parsing import './types.npidl' from main.npidl
void handle_import(const std::string& import_path) {
    auto resolved = resolve_path(ctx_.file_path, import_path);
    
    // Save main.npidl context and switch to types.npidl
    ctx_.push_file(resolved);
    
    // Now ctx_.current_file_path() == "types.npidl"
    // Parse types.npidl...
    
    ctx_.pop_file();  // Return to main.npidl
}
```

### `pop_file()`

**When to call:** After finishing parsing an imported file

**What it does:**
1. Restores previous file context from stack
2. Restores namespace context
3. Pops the stack

**Throws:** If stack is empty (programming error)

### `is_in_import()`

**Returns:** `true` if currently parsing an imported file (not main file)

**Use case:** Conditional logic, diagnostics, debugging

```cpp
if (ctx_.is_in_import()) {
    // We're in an import - maybe skip some validations?
    // Or add "imported from X" to error messages
}
```

### `import_depth()`

**Returns:** Depth of import nesting (0 = main file, 1 = first import, etc.)

**Use case:** Detect circular imports, limit recursion depth

```cpp
if (ctx_.import_depth() > 10) {
    throw parser_error("Import depth too deep - possible circular import");
}
```

### `parent_file_path()`

**Returns:** `optional<path>` - path of file that imported current file

**Use case:** Error messages, import resolution

```cpp
if (auto parent = ctx_.parent_file_path()) {
    std::cerr << "Error in " << ctx_.current_file_path() 
              << " (imported from " << *parent << ")\n";
}
```

## Usage Example: Complete Import Flow

### Example Files

**main.npidl:**
```npidl
import "./types.npidl";

namespace App {
    interface Graphics {
        void draw(in Common::Point p);  // ← Type from import
    };
}
```

**types.npidl:**
```npidl
namespace Common {
    struct Point {
        i32 x;
        i32 y;
    };
}
```

### Parsing Flow

```cpp
// 1. Start parsing main.npidl
Context ctx;
ctx.open("main.npidl");
// Stack: []
// Current: main.npidl, namespace ::

Parser parser(ctx, builder);

// 2. Parser encounters: import "./types.npidl"
bool import_decl() {
    if (peek() != TokenId::Import) return false;
    flush();
    
    auto import_path = match(TokenId::String);  // "./types.npidl"
    match(';');
    
    // Resolve relative to current file
    auto resolved = ctx_.get_file_path().parent_path() / import_path.name;
    
    // Push current file and switch to import
    ctx_.push_file(resolved);
    // Stack: [main.npidl (::)]
    // Current: types.npidl, namespace ::
    
    // Parse the imported file
    parse_imported_file(resolved);
    
    // Pop back to main file
    ctx_.pop_file();
    // Stack: []
    // Current: main.npidl, namespace ::
    
    return true;
}

// 3. parse_imported_file() implementation
void parse_imported_file(const std::filesystem::path& path) {
    // Create parser for imported file
    Context& ctx = /* same context */;
    
    if (is_open_in_editor(path)) {
        // LSP mode: use in-memory content
        Parser parser(get_content(path), ctx, builder, true);
        parser.parse();
    } else {
        // File mode: read from disk
        Parser parser(ctx, builder);
        parser.parse();
    }
    
    // When parser completes, types.npidl is fully parsed
    // Namespace Common::Point is registered in nm_root_
}

// 4. Continue parsing main.npidl
// Parser encounters: namespace App { ... }
// Can now resolve Common::Point because it was registered during import
```

## Namespace Handling

### Key Design Decision

**Imports share the global namespace hierarchy** (`nm_root_` is shared)

```cpp
void push_file(std::filesystem::path new_file_path) {
    // Save current namespace
    file_stack_.push_back(FileContext{
        /* ... */
        .namespace_at_entry = nm_cur_
    });
    
    // Switch file
    file_path = std::move(new_file_path);
    base_name = /* ... */;
    
    // DON'T reset nm_cur_ - keep namespace context!
    // This allows imported files to add to global namespace
}
```

### Example: Namespace Context Preservation

```npidl
// main.npidl
namespace App {
    import "./types.npidl";  // ← Inside namespace App
    
    interface Graphics {
        void draw(in Point p);  // ← Resolves to App::Point
    };
}
```

**Parsing flow:**
1. Enter `namespace App` → `nm_cur_ = ::App`
2. Encounter `import "./types.npidl"`
3. `push_file("types.npidl")` → Save `nm_cur_ = ::App`
4. Parse types.npidl **inside App namespace**
5. `pop_file()` → Restore `nm_cur_ = ::App`
6. Continue parsing Graphics interface

### Alternative: Reset Namespace

If you want imports to start at root namespace:

```cpp
void push_file(std::filesystem::path new_file_path) {
    file_stack_.push_back(FileContext{
        /* ... */
        .namespace_at_entry = nm_cur_
    });
    
    file_path = std::move(new_file_path);
    base_name = /* ... */;
    
    // Reset to root namespace for imported file
    nm_cur_ = nm_root_;  // ← Add this line
}
```

## Error Handling

### Circular Import Detection

```cpp
// Track which files are currently being parsed
std::set<std::filesystem::path> parsing_files_;

bool import_decl() {
    auto resolved = resolve_path(import_path);
    
    // Check for circular import
    if (parsing_files_.contains(resolved)) {
        throw parser_error(
            "Circular import detected: " + resolved.string() + "\n" +
            "Import chain: " + get_import_chain()
        );
    }
    
    parsing_files_.insert(resolved);
    ctx_.push_file(resolved);
    
    parse_imported_file(resolved);
    
    ctx_.pop_file();
    parsing_files_.erase(resolved);
    
    return true;
}

std::string get_import_chain() const {
    std::string chain = ctx_.current_file_path();
    for (int i = ctx_.import_depth() - 1; i >= 0; --i) {
        chain += " → " + /* get file at depth i */;
    }
    return chain;
}
```

### Better Error Messages

```cpp
void throw_error_with_context(const std::string& msg) {
    std::string full_msg = msg;
    
    if (ctx_.is_in_import()) {
        full_msg += "\n  in file: " + ctx_.current_file_path();
        
        if (auto parent = ctx_.parent_file_path()) {
            full_msg += "\n  imported from: " + parent->string();
        }
    }
    
    throw parser_error(full_msg);
}

// Usage:
if (!type) {
    throw_error_with_context("Unknown type '" + name + "'");
}

// Error output:
// Unknown type 'Foo'
//   in file: /project/types.npidl
//   imported from: /project/main.npidl
```

## Import Depth Limiting

Prevent pathological cases:

```cpp
constexpr size_t MAX_IMPORT_DEPTH = 32;

bool import_decl() {
    if (ctx_.import_depth() >= MAX_IMPORT_DEPTH) {
        throw parser_error(
            "Import depth limit exceeded (" + 
            std::to_string(MAX_IMPORT_DEPTH) + ")\n" +
            "Possible circular import or too many nested imports"
        );
    }
    
    // ... rest of import handling
}
```

## Parser Integration

### Adding import_decl to Grammar

```cpp
// EBNF: import_decl ::= 'import' STRING ';'
bool import_decl() {
    if (peek() != TokenId::Import) return false;
    flush();
    
    auto import_path_tok = match(TokenId::String);
    match(';');
    
    // Resolve import path relative to current file
    auto current_dir = ctx_.get_file_path().parent_path();
    auto resolved = std::filesystem::canonical(
        current_dir / import_path_tok.name
    );
    
    // Check if already parsed (avoid duplicate parsing)
    if (already_parsed(resolved)) {
        return true;
    }
    
    // Parse the imported file
    ctx_.push_file(resolved);
    
    try {
        if (auto* doc = doc_manager_.get(path_to_uri(resolved))) {
            // LSP: Use in-memory content
            Parser parser(doc->content, ctx_, builder_, enable_recovery_);
            parser.parse();
        } else {
            // File: Read from disk
            Parser parser(ctx_, builder_);
            parser.parse();
        }
        
        ctx_.pop_file();
    } catch (...) {
        ctx_.pop_file();  // Ensure stack is cleaned up
        throw;
    }
    
    mark_as_parsed(resolved);
    
    return true;
}

// Update stmt_decl to include imports
bool stmt_decl() {
    return (
        check(&Parser::import_decl) ||  // ← Add this
        check(&Parser::const_decl) ||
        check(&Parser::namespace_decl) ||
        /* ... */
    );
}
```

### Tracking Parsed Files

```cpp
class Context {
    // ... existing members ...
    
    std::set<std::filesystem::path> parsed_files_;
    
public:
    bool is_already_parsed(const std::filesystem::path& path) const {
        return parsed_files_.contains(path);
    }
    
    void mark_as_parsed(const std::filesystem::path& path) {
        parsed_files_.insert(path);
    }
};
```

## Testing Strategy

### Unit Test: Stack Operations

```cpp
TEST(Context, FileStackOperations) {
    Context ctx;
    ctx.open("/project/main.npidl");
    
    EXPECT_EQ(ctx.current_file_path(), "/project/main.npidl");
    EXPECT_FALSE(ctx.is_in_import());
    EXPECT_EQ(ctx.import_depth(), 0);
    
    // Push first import
    ctx.push_file("/project/types.npidl");
    EXPECT_EQ(ctx.current_file_path(), "/project/types.npidl");
    EXPECT_TRUE(ctx.is_in_import());
    EXPECT_EQ(ctx.import_depth(), 1);
    EXPECT_EQ(*ctx.parent_file_path(), "/project/main.npidl");
    
    // Push nested import
    ctx.push_file("/project/utils.npidl");
    EXPECT_EQ(ctx.current_file_path(), "/project/utils.npidl");
    EXPECT_EQ(ctx.import_depth(), 2);
    EXPECT_EQ(*ctx.parent_file_path(), "/project/types.npidl");
    
    // Pop back
    ctx.pop_file();
    EXPECT_EQ(ctx.current_file_path(), "/project/types.npidl");
    EXPECT_EQ(ctx.import_depth(), 1);
    
    ctx.pop_file();
    EXPECT_EQ(ctx.current_file_path(), "/project/main.npidl");
    EXPECT_FALSE(ctx.is_in_import());
}
```

### Integration Test: Imports

```cpp
TEST(Parser, BasicImport) {
    // Create files
    create_file("/tmp/types.npidl", R"(
        namespace Common {
            struct Point { i32 x; i32 y; };
        }
    )");
    
    create_file("/tmp/main.npidl", R"(
        import "./types.npidl";
        
        namespace App {
            interface Graphics {
                void draw(in Common::Point p);
            };
        }
    )");
    
    // Parse
    Context ctx;
    ctx.open("/tmp/main.npidl");
    BuildGroup builder(&ctx);
    Parser parser(ctx, builder);
    parser.parse();
    
    // Verify
    EXPECT_FALSE(ctx.is_in_import());  // Back to main
    auto* point_type = ctx.nm_root()->find_type("Common::Point", true);
    EXPECT_NE(point_type, nullptr);
}
```

## Summary

### What Was Added to Context

```cpp
struct FileContext {
    std::filesystem::path file_path;
    std::string base_name;
    Namespace* namespace_at_entry;
};

std::vector<FileContext> file_stack_;

// API:
void push_file(std::filesystem::path);    // Enter import
void pop_file();                           // Exit import
bool is_in_import() const;                 // Check if in import
size_t import_depth() const;               // Get nesting level
optional<path> parent_file_path() const;   // Get importing file
```

### How It Works

1. **Main file:** Stack empty, `import_depth() == 0`
2. **Import encountered:** `push_file()` saves current context
3. **Parse import:** Context points to imported file
4. **Import completes:** `pop_file()` restores previous context
5. **Nested imports:** Stack grows, each level tracked

### Benefits

✅ **Clean separation** - File context isolated from namespace/symbols
✅ **Error reporting** - Know exactly which file has error
✅ **Circular detection** - Track parsing chain
✅ **Namespace preservation** - Imports can add to any namespace
✅ **LSP compatible** - Works with in-memory and file-based parsing
✅ **Exception safe** - Can use RAII guards for push/pop

### Next Steps

1. Add `TokenId::Import` to lexer
2. Implement `import_decl()` in parser
3. Add `already_parsed` tracking to Context
4. Add circular import detection
5. Test with nested imports
6. Integrate with LSP DocumentManager

The foundation is ready! 🎉
