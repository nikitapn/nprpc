# NPIDL Language Server Protocol (LSP) Implementation

This directory contains the LSP server implementation for the NPIDL language, providing IDE features like syntax highlighting, diagnostics, hover information, and go-to-definition for `.npidl` files.

## Features

### Current (MVP)
- ✅ **Protocol Implementation**: Full JSON-RPC 2.0 LSP protocol
- ✅ **Document Synchronization**: Full document sync for open/change/close
- ✅ **Server Capabilities**: Advertises hover, definition, references, document symbols
- ✅ **Message I/O**: Content-Length protocol over stdin/stdout

### In Progress
- 🔄 **Diagnostics**: Syntax errors and validation (requires parser integration)
- 🔄 **Hover**: Type information at cursor position
- 🔄 **Go-to-Definition**: Navigate to symbol declarations
- 🔄 **Find References**: Find all usages of a symbol
- 🔄 **Document Symbols**: Outline view of interfaces, structs, methods

## Architecture

```
┌─────────────────────────────────────────────┐
│  Editor (Neovim/Emacs/VSCode)               │
└──────────────┬──────────────────────────────┘
               │ JSON-RPC over stdin/stdout
               │
┌──────────────▼──────────────────────────────┐
│  LspServer                                  │
│  - Message I/O (Content-Length protocol)    │
│  - Request/Notification routing             │
│  - JSON serialization (glaze library)       │
└──────────────┬──────────────────────────────┘
               │
┌──────────────▼──────────────────────────────┐
│  DocumentManager                            │
│  - Document lifecycle (open/change/close)   │
│  - Parse documents and collect diagnostics  │
│  - Symbol table for hover/definition        │
└──────────────┬──────────────────────────────┘
               │
┌──────────────▼──────────────────────────────┐
│  Parser (existing NPIDL parser)             │
│  - Syntax analysis                          │
│  - AST generation                           │
│  - Error collection (to be modified)        │
└─────────────────────────────────────────────┘
```

## Usage

### Command Line

```bash
# Start LSP server (typically called by editor)
npidl --lsp

# Normal compiler mode (default)
npidl input.npidl output_dir
```

### Neovim Configuration

1. Install `nvim-lspconfig` plugin
2. Copy `neovim-lsp-config.lua` to your Neovim config directory
3. Update the path to the npidl binary in the config
4. Open any `.npidl` file and LSP should activate automatically

**Quick Setup:**

```lua
-- In ~/.config/nvim/init.lua or ~/.config/nvim/lua/init.lua
require('lsp-npidl')
```

See `neovim-lsp-config.lua` for the full configuration.

### Emacs Configuration

1. Install `lsp-mode` package
2. Add the contents of `emacs-lsp-config.el` to your Emacs config
3. Update the path to the npidl binary in the config
4. Open any `.npidl` file and run `M-x lsp`

See `emacs-lsp-config.el` for the full configuration.

### VSCode Configuration

A complete VSCode extension is provided in the `vscode-extension/` directory.

**Quick Setup:**

```bash
cd vscode-extension
./setup.sh
```

This will install dependencies and compile the extension. Then:

**Option 1: Install as VSIX Package**
```bash
npm install -g @vscode/vsce
vsce package
code --install-extension npidl-lsp-*.vsix
```

**Option 2: Development Mode**
1. Open the `vscode-extension` folder in VSCode
2. Press F5 to launch Extension Development Host
3. Open any `.npidl` file in the new window

**Configuration:**

After installation, configure the npidl path in VSCode settings (File > Preferences > Settings):

```json
{
  "npidl.lsp.path": "/path/to/npsystem/build/linux/bin/npidl"
}
```

The extension provides:
- Syntax highlighting for `.npidl` files
- Automatic LSP server startup
- Full LSP feature support (diagnostics, hover, go-to-definition, etc.)
- Debug output in the Output panel

See `vscode-extension/README.md` for detailed documentation.

## Building

The LSP server is built as part of the npidl compiler:

```bash
cd /path/to/npsystem/build/linux
cmake --build . --target npidl
```

**Requirements:**
- C++23 compiler (for glaze JSON library)
- CMake 4.0+
- Boost.ProgramOptions

## Testing

### Manual Test

```bash
# Test initialization
printf 'Content-Length: 107\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}' | ./npidl --lsp
```

Expected output:
```json
Content-Length: 201

{"jsonrpc":"2.0","id":1,"result":{"capabilities":{"textDocumentSync":{"openClose":1,"change":1},"hoverProvider":true,"definitionProvider":true,"referencesProvider":true,"documentSymbolProvider":true}}}
```

### Integration Test

Use the provided `test_lsp.sh` script in the project root:

```bash
./test_lsp.sh
```

## LSP Protocol Details

### Supported Methods

#### Requests (client → server)
- `initialize` - Handshake and capability negotiation
- `shutdown` - Graceful shutdown request
- `textDocument/hover` - Show type information at cursor (TODO: implement)
- `textDocument/definition` - Go to symbol definition (TODO: implement)

#### Notifications (client → server)
- `initialized` - Client is ready
- `exit` - Force exit
- `textDocument/didOpen` - Document opened in editor
- `textDocument/didChange` - Document content changed
- `textDocument/didClose` - Document closed in editor

#### Notifications (server → client)
- `textDocument/publishDiagnostics` - Send syntax errors and warnings

### Message Format

All messages follow the LSP Content-Length protocol:

```
Content-Length: <byte-count>\r\n
\r\n
<JSON-RPC message>
```

JSON-RPC 2.0 format:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/hover",
  "params": { ... }
}
```

## Implementation Status

### Completed ✅
- [x] LSP protocol structures with glaze serialization
- [x] Message I/O (Content-Length protocol)
- [x] Request/notification routing
- [x] Document lifecycle management
- [x] Server capabilities advertisement
- [x] Integration with npidl build system

### Next Steps 🚧

1. **Parser Integration** (2-3 days)
   - Modify Parser to collect errors instead of throwing exceptions
   - Store diagnostics with line/column information
   - Send diagnostics to editor on didOpen/didChange

2. **Hover Implementation** (1-2 days)
   - Query AST for symbol at position
   - Extract type information from Context
   - Format and return hover markup

3. **Go-to-Definition** (1-2 days)
   - Build symbol table during parsing
   - Map positions to symbol declarations
   - Return location of definition

4. **Document Symbols** (1 day)
   - Walk AST and collect interfaces/structs/methods
   - Return hierarchical outline for editor

5. **Testing** (1-2 days)
   - Test with real .npidl files
   - Verify diagnostics accuracy
   - Test hover and definition navigation
   - Create comprehensive test suite

## Dependencies

- **glaze** (3rd/glaze): Modern C++ JSON library for fast serialization
  - Header-only, requires C++23
  - Used for all LSP message serialization/deserialization
  
- **Boost.ProgramOptions**: Command-line parsing for --lsp flag

## Files

- `lsp_server.hpp` - LSP protocol structures and server interface
- `lsp_server.cpp` - LSP server implementation
- `main.cpp` - Entry point with --lsp mode
- `neovim-lsp-config.lua` - Neovim configuration example
- `emacs-lsp-config.el` - Emacs configuration example
- `README.md` - This file

## Contributing

When adding new LSP features:

1. Define protocol structures in `lsp_server.hpp` with glaze metadata
2. Add handler method to `LspServer` class
3. Route messages in `run()` method
4. Test with manual LSP messages
5. Verify in actual editor

## References

- [LSP Specification](https://microsoft.github.io/language-server-protocol/)
- [glaze JSON library](https://github.com/stephenberry/glaze)
- [Neovim LSP Guide](https://neovim.io/doc/user/lsp.html)
- [Emacs lsp-mode](https://emacs-lsp.github.io/lsp-mode/)
