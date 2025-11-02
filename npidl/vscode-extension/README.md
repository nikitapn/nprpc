# NPIDL Language Support for VSCode

This extension provides Language Server Protocol (LSP) support for NPIDL files in Visual Studio Code.

## Features

- **Syntax Highlighting** for `.npidl` files
- **Error Diagnostics** (syntax errors and validation)
- **Hover Information** (type information at cursor)
- **Go to Definition** (navigate to symbol declarations)
- **Find References** (find all usages of symbols)
- **Document Symbols** (outline view of interfaces and structs)

## Requirements

You need to have the `npidl` compiler installed with LSP support. The extension will look for the `npidl` executable in your PATH by default.

## Installation

### From Source

1. Clone the repository
2. Navigate to `nprpc/npidl/vscode-extension/`
3. Install dependencies:
   ```bash
   npm install
   ```
4. Compile the extension:
   ```bash
   npm run compile
   ```
5. Install the extension:
   ```bash
   code --install-extension .
   ```

### From VSIX Package

1. Package the extension:
   ```bash
   npm install -g @vscode/vsce
   vsce package
   ```
2. Install the generated `.vsix` file:
   ```bash
   code --install-extension npidl-lsp-0.1.0.vsix
   ```

## Configuration

Open VSCode settings (File > Preferences > Settings) and search for "npidl":

- **npidl.lsp.path**: Path to the npidl executable (default: "npidl")
  - If npidl is not in your PATH, set the full path:
    ```json
    {
      "npidl.lsp.path": "/path/to/npsystem/build/linux/bin/npidl"
    }
    ```

- **npidl.lsp.trace.server**: Trace LSP communication for debugging
  - Options: "off", "messages", "verbose"

## Usage

1. Open any `.npidl` file in VSCode
2. The extension will automatically start the NPIDL language server
3. You'll see:
   - Syntax errors highlighted in the editor
   - Type information when hovering over symbols
   - Code completion and navigation features

## Keyboard Shortcuts

- **F12** or **Ctrl+Click**: Go to Definition
- **Shift+F12**: Find All References
- **Ctrl+K Ctrl+I**: Show Hover Information
- **Ctrl+Shift+O**: Go to Symbol in File

## Development

### Building

```bash
npm install
npm run compile
```

### Watching for Changes

```bash
npm run watch
```

### Testing in Development Mode

1. Open the extension folder in VSCode
2. Press F5 to launch Extension Development Host
3. Open a `.npidl` file in the new window

## Troubleshooting

### Extension not activating

- Check that the npidl executable is in your PATH or configured correctly
- Verify that npidl supports the `--lsp` flag:
  ```bash
  npidl --lsp
  ```

### No diagnostics or features working

- Check the Output panel (View > Output) and select "NPIDL Language Server" from the dropdown
- Enable verbose tracing in settings: `"npidl.lsp.trace.server": "verbose"`

### LSP server crashes

- Check the npidl LSP server logs
- Ensure you're using a compatible version of npidl with LSP support

## Contributing

Issues and pull requests are welcome at the main npsystem repository.

## License

Same license as the npsystem project.
