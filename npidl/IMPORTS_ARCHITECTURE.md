# Import System Architecture for NPIDL

## Current State

The parser now has **dual-mode support**:

### 1. File-Based Parsing (Compiler Mode)
```cpp
Context ctx;
ctx.open("/path/to/file.npidl");
Parser parser(ctx, builder);  // Lexer reads from disk
parser.parse();
```

### 2. In-Memory Parsing (LSP Mode)
```cpp
Context ctx;
ctx.set_file_path("/path/to/file.npidl");  // For error reporting
std::string content = "interface Foo {}";   // From LSP client
Parser parser(content, ctx, builder, true);  // Lexer uses in-memory content
parser.parse();
```

## LSP Protocol & File URIs

### LSP Communication Model

**Important:** LSP is bidirectional, but with constraints:

```
┌─────────────────────────────────────────────────────────┐
│ Client → Server (Requests & Notifications)              │
│  • initialize, textDocument/hover, definition, etc.     │
│  • textDocument/didOpen, didChange, didClose            │
│  • Client initiates, server responds or processes       │
└─────────────────────────────────────────────────────────┘
         ↓
         ↑
┌─────────────────────────────────────────────────────────┐
│ Server → Client (Notifications only for diagnostics)    │
│  • textDocument/publishDiagnostics                      │
│  • window/showMessage, window/logMessage                │
│  • Server cannot request file content from client!      │
└─────────────────────────────────────────────────────────┘
```

**Key Constraint:** Server **cannot** request arbitrary file content.
- ❌ No LSP method for "give me content of file X"
- ✅ Server can only use content from `didOpen`/`didChange` notifications
- ✅ Server can (and should) read closed files from disk

### How LSP Handles Files

The LSP client **sends absolute URIs AND content for open files**:

```typescript
// textDocument/didOpen notification
{
  textDocument: {
    uri: "file:///home/user/project/main.npidl",  // ← Absolute path
    languageId: "npidl",
    version: 1,
    text: "interface Foo { }"  // ← Full content in memory
  }
}
```

**Key Points:**
- ✅ LSP provides absolute file paths (as URIs)
- ✅ LSP provides full file content
- ✅ Server never reads from disk (client controls content)
- ✅ Handles unsaved changes, new files, remote files

### URI Format

LSP uses standard file URIs:
```
file:///home/user/project/main.npidl       (Linux/Mac)
file:///c%3A/Users/user/project/main.npidl (Windows, encoded)
```

You can convert between URI and path:
```cpp
std::string uri_to_path(const std::string& uri) {
    if (uri.starts_with("file://")) {
        return url_decode(uri.substr(7));  // Remove "file://"
    }
    return uri;
}
```

## Future Import System Design

### Syntax Options

```npidl
// Option 1: Relative imports (recommended)
import "./types.npidl";
import "../common/base.npidl";

// Option 2: Absolute imports (more complex)
import "project/types.npidl";

// Option 3: Named imports (future enhancement)
import { Calculator, Point } from "./types.npidl";
```

### Architecture for Imports

#### Phase 1: Single-File Support (Current)

```
┌─────────────────────────────────────┐
│ LSP Client (VSCode)                 │
│  - Sends file URI                   │
│  - Sends file content               │
└──────────────┬──────────────────────┘
               │
               ↓
┌─────────────────────────────────────┐
│ LSP Server (npidl --lsp)            │
│  - Parses in-memory content         │
│  - Context has file path            │
│  - No imports yet                   │
└─────────────────────────────────────┘
```

#### Phase 2: Multi-File with Imports

```
┌──────────────────────────────────────────────────┐
│ LSP Client                                       │
│  - DocumentManager tracks ALL open files        │
│  - Sends content for each file                  │
└─────────────────┬────────────────────────────────┘
                  │
                  ↓
┌──────────────────────────────────────────────────┐
│ LSP Server: Import Resolver                     │
│                                                  │
│  1. Parse main file, encounter import           │
│     "import './types.npidl'"                     │
│                                                  │
│  2. Resolve relative path:                      │
│     main:  /home/user/project/main.npidl        │
│     import: ./types.npidl                       │
│     → resolved: /home/user/project/types.npidl  │
│                                                  │
│  3. Check DocumentManager cache:                │
│     a) If open: Use in-memory content ✓         │
│     b) If not: Read from disk ✓                 │
│                                                  │
│  4. Parse imported file recursively             │
│                                                  │
│  5. Build unified symbol table                  │
│                                                  │
│  6. Return diagnostics for ALL files            │
└──────────────────────────────────────────────────┘
```

### Implementation Plan

#### 1. Add Import Statement to Grammar

```cpp
// In Parser class
bool import_decl() {
    if (peek() != TokenId::Import) return false;
    flush();
    
    auto path_tok = match(TokenId::String);  // "./types.npidl"
    match(';');
    
    // Resolve path relative to current file
    auto imported_path = resolve_import_path(
        ctx_.current_file_path(), 
        path_tok.name
    );
    
    // Tell context about the import
    ctx_.add_import(imported_path);
    
    return true;
}

// In parse() method
bool stmt_decl() {
    return import_decl() ||
           namespace_decl() ||
           interface_decl(attr) ||
           // ... etc
}
```

#### 2. Extend Context for Import Management

```cpp
class Context {
    std::filesystem::path current_file_;
    std::set<std::filesystem::path> imported_files_;  // Cycle detection
    std::map<std::filesystem::path, Namespace*> file_namespaces_;
    
public:
    std::filesystem::path resolve_import(const std::string& import_path) {
        // "./types.npidl" relative to current_file_
        return current_file_.parent_path() / import_path;
    }
    
    void add_import(const std::filesystem::path& path) {
        if (imported_files_.contains(path)) {
            throw parser_error("Circular import detected: " + path.string());
        }
        imported_files_.insert(path);
    }
    
    bool is_imported(const std::filesystem::path& path) const {
        return imported_files_.contains(path);
    }
};
```

#### 3. Import Resolver (Two Modes)

**Compiler Mode (reads from disk):**
```cpp
class ImportResolver {
    Context& ctx_;
    BuildGroup& builder_;
    
public:
    void resolve_imports(const std::filesystem::path& main_file) {
        parse_file(main_file);
        
        // Recursively parse all imports
        while (!ctx_.pending_imports().empty()) {
            auto import = ctx_.pending_imports().pop();
            parse_file(import);
        }
    }
    
private:
    void parse_file(const std::filesystem::path& path) {
        if (ctx_.is_imported(path)) return;  // Already parsed
        
        ctx_.open(path);
        Parser parser(ctx_, builder_);
        parser.parse();  // Will add more imports to ctx_
    }
};
```

**LSP Mode (uses DocumentManager):**
```cpp
class LspImportResolver {
    DocumentManager& docs_;
    Context& ctx_;
    
public:
    void resolve_imports(const std::string& main_uri) {
        parse_document(main_uri);
        
        while (!ctx_.pending_imports().empty()) {
            auto import_path = ctx_.pending_imports().pop();
            auto import_uri = path_to_uri(import_path);
            parse_document(import_uri);
        }
    }
    
private:
    void parse_document(const std::string& uri) {
        if (ctx_.is_imported(uri_to_path(uri))) return;
        
        // Try to get from open documents first
        auto* doc = docs_.get(uri);
        if (doc) {
            // File is open in editor - use in-memory content (may have unsaved changes)
            NullBuilder builder(&ctx_);
            Parser parser(doc->content, ctx_, builder, true);
            parser.parse();
        } else {
            // File not open - read from disk (standard LSP practice)
            // Note: Won't see unsaved changes if file is open in another editor
            // but this is acceptable - imports are typically stable
            auto path = uri_to_path(uri);
            if (!std::filesystem::exists(path)) {
                // Import not found - report diagnostic
                return;
            }
            ctx_.open(path);
            NullBuilder builder(&ctx_);
            Parser parser(ctx_, builder);
            parser.parse();
        }
    }
};
```

#### 4. Symbol Resolution Across Files

```cpp
// Current: Find type in current namespace only
auto* type = ctx_.nm_cur()->find_type(name, recursive);

// With imports: Search imported namespaces
auto* type = ctx_.find_type_with_imports(name);

// Implementation in Context
AstTypeDecl* Context::find_type_with_imports(const std::string& name) {
    // 1. Search current file's namespace
    if (auto* type = nm_cur()->find_type(name, true)) {
        return type;
    }
    
    // 2. Search imported files' namespaces
    for (auto& [file, ns] : imported_namespaces_) {
        if (auto* type = ns->find_type(name, true)) {
            return type;
        }
    }
    
    return nullptr;
}
```

### Example Workflow

**File: types.npidl**
```npidl
namespace Common {
    struct Point {
        i32 x;
        i32 y;
    };
}
```

**File: main.npidl**
```npidl
import "./types.npidl";

namespace App {
    interface Graphics {
        void draw(in Common::Point p);  // ← Type from import
    };
}
```

**Parsing Flow:**

1. **LSP client opens main.npidl**:
   ```
   didOpen: { uri: ".../main.npidl", text: "import..." }
   ```

2. **Server parses main.npidl**:
   - Encounters `import "./types.npidl"`
   - Resolves to absolute path: `.../types.npidl`
   - Checks DocumentManager: types.npidl not open
   - Reads from disk (or requests from client)
   
3. **Server parses types.npidl**:
   - Builds namespace `Common`
   - Registers type `Common::Point`
   
4. **Server continues parsing main.npidl**:
   - Encounters `Common::Point` in Graphics.draw()
   - Searches: current namespace → imports
   - Finds `Common::Point` from types.npidl ✓
   
5. **Server sends diagnostics**:
   - No errors for main.npidl
   - No errors for types.npidl

### Challenges & Solutions

#### Challenge 1: Circular Imports
```npidl
// a.npidl
import "./b.npidl";

// b.npidl
import "./a.npidl";  // ← Circular!
```

**Solution:**
```cpp
std::set<std::filesystem::path> parsing_stack_;

void parse_file(const std::filesystem::path& path) {
    if (parsing_stack_.contains(path)) {
        throw parser_error("Circular import: " + path.string());
    }
    
    parsing_stack_.insert(path);
    // ... parse ...
    parsing_stack_.erase(path);
}
```

#### Challenge 2: Import Ordering
```npidl
import "./a.npidl";  // Defines type A
import "./b.npidl";  // Uses type A
```

**Solution:** Parse all imports first, then type-check:
```cpp
// Phase 1: Parse all files (build AST)
for (auto& file : all_files) {
    parse_file_ast_only(file);
}

// Phase 2: Type-check with full symbol table
for (auto& file : all_files) {
    type_check_file(file);
}
```

#### Challenge 3: Reading Imported Files

**Q: Can server request file content from client?**
**A: No!** LSP has no mechanism for this.

**Standard Practice:** Read from disk!

```cpp
void resolve_import(const std::string& import_path) {
    auto resolved = resolve_path(import_path);
    auto uri = path_to_uri(resolved);
    
    // Check if file is open in editor
    if (auto* doc = docs_.get(uri)) {
        // ✅ Use live content (includes unsaved changes)
        parse(doc->content, resolved);
    } else {
        // ✅ Read from disk (standard practice!)
        if (std::filesystem::exists(resolved)) {
            std::ifstream ifs(resolved);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
            parse(content, resolved);
        } else {
            // Send diagnostic: "Cannot resolve import"
            send_diagnostic(original_uri, "Import not found: " + import_path);
        }
    }
}
```

**Real-world examples:**
- **TypeScript** (`tsserver`): Reads `node_modules` from disk
- **Rust** (`rust-analyzer`): Reads dependencies from disk
- **Python** (`pyright`): Reads library files from disk
- **C++** (`clangd`): Reads header files from disk

**Why this is fine:**
- User is actively editing the main file (open in editor)
- Imported files are typically stable (saved to disk)
- If user opens an imported file, server receives `didOpen` notification
- Server can then re-validate dependent files with live content

### Challenge 4: Incremental Updates (LSP)

When user edits types.npidl:
```
1. Re-parse types.npidl
2. Find all files that import types.npidl
3. Re-validate those files (symbol resolution)
4. Send updated diagnostics
```

**Implementation:**
```cpp
class DocumentManager {
    // Track reverse dependencies
    std::map<std::string, std::set<std::string>> import_graph_;
    // file → [files that import it]
    
    void on_document_changed(const std::string& uri) {
        // Re-parse changed file
        parse_document(uri);
        
        // Re-validate dependent files
        for (auto& dependent : import_graph_[uri]) {
            validate_document(dependent);
        }
    }
};
```

## Migration Path

### Stage 1: ✅ Current (Complete)
- Dual Lexer constructors (file vs in-memory)
- Dual Parser constructors
- Single-file parsing works

### Stage 2: Basic Imports (2-3 days)
- Add `import` keyword to lexer
- Add `import_decl()` to parser
- Path resolution (relative paths only)
- Recursive parsing (compiler mode)

### Stage 3: LSP Integration (2-3 days)
- Integrate with DocumentManager
- Cache open documents
- Fallback to disk for closed files
- Dependency tracking

### Stage 4: Incremental Updates (1-2 days)
- Build import graph
- Detect affected files on change
- Re-validate dependents
- Optimize re-parsing

### Stage 5: Advanced Features (Future)
- Named imports: `import { A, B } from "./file"`
- Wildcard imports: `import * from "./file"`
- Namespace aliases: `import Common as C`
- Cross-workspace imports (LSP multi-root)

## Testing Strategy

### Unit Tests
```cpp
TEST(Imports, BasicImport) {
    create_file("types.npidl", "struct Point { i32 x; };");
    create_file("main.npidl", "import './types.npidl'; Point p;");
    
    auto result = compile("main.npidl");
    ASSERT_FALSE(result.has_errors());
}

TEST(Imports, CircularDetection) {
    create_file("a.npidl", "import './b.npidl';");
    create_file("b.npidl", "import './a.npidl';");
    
    auto result = compile("a.npidl");
    ASSERT_TRUE(result.has_errors());
    EXPECT_THAT(result.error(), HasSubstr("Circular import"));
}
```

### Integration Tests
```bash
# Test LSP with imports
echo '{"method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///main.npidl","text":"import \"./types.npidl\";\ninterface Foo { void bar(Point p); };"}}}' | npidl --lsp

# Should return diagnostics for both files
```

## Conclusion

**Answer to Your Question:**

> Does LSP client send absolute path to the file?

**Yes!** LSP sends:
1. **Absolute URI** (e.g., `file:///home/user/project/main.npidl`)
2. **Full content** (in-memory, even for unsaved changes)

This makes imports straightforward:
- You have the absolute path of the main file
- Resolve relative imports against that absolute path
- Check DocumentManager for open files (in-memory)
- Fall back to disk for closed files

**Current State:** ✅ Architecture ready for imports
- Lexer accepts in-memory content
- Parser accepts in-memory content
- Context tracks current file path
- Just need to add import parsing logic!

**Recommendation:** Start with simple relative imports (`"./file.npidl"`) and expand from there. The foundation is solid!
