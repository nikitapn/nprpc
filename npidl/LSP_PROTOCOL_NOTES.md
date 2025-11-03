# LSP Protocol: Communication Patterns

## TL;DR

- ✅ LSP **is** bidirectional (requests can go both ways)
- ❌ Server **cannot** request arbitrary file content from client
- ✅ Server **should** read non-open files from disk (standard practice)
- ✅ Server **can** send notifications (diagnostics, messages) to client

## Communication Directions

### Client → Server (Primary Direction)

#### Requests (client asks, server responds)
```json
// Client: "What's the type at this position?"
→ {"id": 1, "method": "textDocument/hover", "params": {...}}
← {"id": 1, "result": {"contents": "interface Calculator"}}

// Client: "Where is this symbol defined?"
→ {"id": 2, "method": "textDocument/definition", "params": {...}}
← {"id": 2, "result": {"uri": "...", "range": {...}}}
```

#### Notifications (client tells server, no response)
```json
// Client: "I opened a file"
→ {"method": "textDocument/didOpen", "params": {
    "textDocument": {
      "uri": "file:///project/main.npidl",
      "text": "interface Foo { };"
    }
  }}

// Client: "User edited the file"
→ {"method": "textDocument/didChange", "params": {...}}

// Client: "User closed the file"
→ {"method": "textDocument/didClose", "params": {...}}
```

### Server → Client

#### Notifications (server tells client, no response)
```json
// Server: "Here are syntax errors"
→ {"method": "textDocument/publishDiagnostics", "params": {
    "uri": "file:///project/main.npidl",
    "diagnostics": [
      {"range": {...}, "message": "Expected semicolon", "severity": 1}
    ]
  }}

// Server: "Show this message to user"
→ {"method": "window/showMessage", "params": {
    "type": 3,  // Info
    "message": "NPIDL compilation complete"
  }}

// Server: "Log this for debugging"
→ {"method": "window/logMessage", "params": {
    "type": 4,  // Log
    "message": "Parsing file: main.npidl"
  }}
```

#### Requests (server asks, client responds) - **RARE!**
```json
// Server: "Please apply this edit"
→ {"id": 100, "method": "workspace/applyEdit", "params": {
    "edit": {"changes": {...}}
  }}
← {"id": 100, "result": {"applied": true}}

// Server: "Show dialog to user"
→ {"id": 101, "method": "window/showMessageRequest", "params": {
    "message": "Import not found. Read from disk?",
    "actions": [{"title": "Yes"}, {"title": "No"}]
  }}
← {"id": 101, "result": {"title": "Yes"}}

// Server: "Register new capability dynamically"
→ {"id": 102, "method": "client/registerCapability", "params": {...}}
← {"id": 102, "result": null}
```

## What Server **Cannot** Do

### ❌ Request File Content

**No LSP method for this:**
```cpp
// ❌ DOESN'T EXIST
std::string content = lsp_client.request_file("file:///project/types.npidl");
```

**Why?** 
- LSP philosophy: Client controls what server sees
- Server should not access arbitrary files
- Prevents security/privacy issues

### What to Do Instead

```cpp
// ✅ Option 1: Check if file is open (in DocumentManager)
if (auto* doc = doc_manager.get(uri)) {
    // File is open - use content from didOpen/didChange
    use_content(doc->content);
}

// ✅ Option 2: Read from disk
else if (std::filesystem::exists(path)) {
    std::ifstream ifs(path);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
    use_content(content);
}

// ⚠️ Option 3: Report error
else {
    send_diagnostic(uri, "File not found");
}
```

## Import Resolution Pattern

### The Right Way to Handle Imports

```cpp
class DocumentManager {
    std::map<std::string, Document> open_docs_;  // URI → Document
    
public:
    void handle_import(const std::string& import_path, 
                      const std::string& from_uri) {
        // 1. Resolve import path
        auto from_path = uri_to_path(from_uri);
        auto resolved = from_path.parent_path() / import_path;
        auto resolved_uri = path_to_uri(resolved);
        
        // 2. Try DocumentManager first (open files)
        if (auto* doc = open_docs_.find(resolved_uri); 
            doc != open_docs_.end()) {
            // ✅ File is open - use live content
            parse(doc->second.content, resolved);
            return;
        }
        
        // 3. Fall back to disk (standard practice!)
        if (!std::filesystem::exists(resolved)) {
            // Cannot resolve - send diagnostic
            lsp::Diagnostic diag;
            diag.message = "Cannot resolve import: " + import_path;
            diag.severity = 1;  // Error
            publish_diagnostics(from_uri, {diag});
            return;
        }
        
        // 4. Read from disk
        std::ifstream ifs(resolved);
        std::string content((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
        parse(content, resolved);
    }
};
```

### When File Opens Later

```cpp
void on_did_open(const lsp::DidOpenTextDocumentParams& params) {
    // Store document
    Document doc;
    doc.uri = params.textDocument.uri;
    doc.content = params.textDocument.text;
    doc.version = params.textDocument.version;
    
    open_docs_[doc.uri] = doc;
    
    // Parse and send diagnostics
    auto errors = parse_for_lsp(doc.content);
    publish_diagnostics(doc.uri, to_lsp_diagnostics(errors));
    
    // IMPORTANT: Re-validate files that import this one!
    for (const auto& dependent_uri : find_dependents(doc.uri)) {
        auto* dependent = open_docs_.find(dependent_uri);
        if (dependent != open_docs_.end()) {
            // Re-parse dependent with updated import
            auto errors = parse_for_lsp(dependent->second.content);
            publish_diagnostics(dependent_uri, to_lsp_diagnostics(errors));
        }
    }
}
```

## Real-World Examples

### TypeScript Server (`tsserver`)

```typescript
// main.ts (open in editor)
import { Calculator } from './calc';  // ← calc.ts NOT open

// LSP Server Behavior:
// 1. Receives didOpen for main.ts with full content
// 2. Encounters import './calc'
// 3. Resolves to ./calc.ts
// 4. Checks if calc.ts is open → NO
// 5. Reads calc.ts from disk ✅
// 6. If user later opens calc.ts, receives didOpen
// 7. Re-validates main.ts with live calc.ts content
```

### Rust Analyzer

```rust
// main.rs (open)
use crate::utils::helper;  // ← utils.rs NOT open

// LSP Server:
// 1. Parses main.rs from didOpen
// 2. Resolves use crate::utils
// 3. Reads src/utils.rs from disk ✅
// 4. Builds complete symbol table
```

### Python Language Server (`pyright`)

```python
# main.py (open)
from typing import List  # ← typeshed NOT open
import mymodule          # ← mymodule.py NOT open

# LSP Server:
# 1. Parses main.py from didOpen
# 2. Reads typing stubs from disk ✅
# 3. Reads mymodule.py from disk ✅
# 4. Provides type checking
```

## Summary Table

| Operation | Available? | Usage |
|-----------|-----------|-------|
| **Client → Server** | | |
| Request (hover, definition, etc.) | ✅ | Primary communication |
| Notification (didOpen, didChange) | ✅ | Document lifecycle |
| | | |
| **Server → Client** | | |
| Notification (diagnostics, messages) | ✅ | Report errors/info |
| Request (applyEdit, showDialog) | ✅ | Rare, specific use cases |
| **Request arbitrary file** | ❌ | Not in LSP spec |
| | | |
| **Alternatives for Files** | | |
| Check DocumentManager | ✅ | For open files |
| Read from disk | ✅ | For closed files (standard!) |
| Report diagnostic | ✅ | If file not found |

## Key Takeaways

1. **LSP is bidirectional** but asymmetric
   - Client → Server: Most requests
   - Server → Client: Mostly notifications

2. **Server cannot request arbitrary file content**
   - No LSP method for this
   - By design (security/control)

3. **Reading from disk is standard practice**
   - All major LSP servers do this
   - Necessary for imports/includes
   - Open files take precedence

4. **DocumentManager is your source of truth for open files**
   - Tracks live editor state
   - Includes unsaved changes
   - Fall back to disk for closed files

5. **Dependency tracking enables smart updates**
   - When imported file opens → re-validate dependents
   - When imported file changes → re-validate dependents
   - Build import graph for efficiency

## Implementation Checklist

For NPIDL LSP server with imports:

- [x] Handle `didOpen`/`didChange`/`didClose` notifications
- [x] Store open documents in `DocumentManager`
- [ ] Parse imports from document content
- [ ] Resolve import paths relative to current file
- [ ] Check `DocumentManager` for open imported files
- [ ] Read closed imported files from disk
- [ ] Build dependency graph (file → files that import it)
- [ ] Re-validate dependents when imported file changes
- [ ] Send diagnostics for unresolved imports
- [ ] Support both open and closed files in same import chain

## Conclusion

**Your initial intuition was correct:** LSP server reads from stdin and handles requests. It **cannot** initiate arbitrary requests to the client for file content.

The solution is simple: **Read from disk for non-open files**. This is not a workaround or hack - it's how **every** LSP server with imports/includes works.

The LSP protocol design makes this pattern natural:
- Open files → Live content from editor (via `didOpen`/`didChange`)
- Closed files → Stable content from disk (via filesystem)

This gives you the best of both worlds: real-time analysis of active files, with complete project understanding from disk.
