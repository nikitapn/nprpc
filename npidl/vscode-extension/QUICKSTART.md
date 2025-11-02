# NPIDL VSCode Extension - Quick Start

This guide will help you get NPIDL language support working in VSCode in under 5 minutes.

## Prerequisites

- Visual Studio Code installed
- Node.js and npm installed (version 18 or higher)
- npidl compiler built with LSP support

## Installation Steps

### 1. Build the npidl compiler (if not already done)

```bash
cd /path/to/npsystem
./build.sh
```

The npidl binary will be at `build/linux/bin/npidl`.

### 2. Set up the VSCode extension

```bash
cd /path/to/npsystem/nprpc/npidl/vscode-extension
./setup.sh
```

This installs dependencies and compiles the TypeScript code.

### 3. Install the extension

**Option A: Install as package (recommended for regular use)**

```bash
# Install vsce if you don't have it
npm install -g @vscode/vsce

# Package the extension
vsce package

# Install in VSCode
code --install-extension npidl-lsp-0.1.0.vsix
```

**Option B: Run in development mode (for testing/development)**

1. Open the `vscode-extension` folder in VSCode
2. Press `F5` - this opens a new "Extension Development Host" window
3. Open or create a `.npidl` file in the new window

### 4. Configure the npidl path

Open VSCode Settings (`Ctrl+,` or `Cmd+,` on Mac) and search for "npidl":

Set the path to your npidl binary:

```json
{
  "npidl.lsp.path": "/home/nikita/projects/npsystem/build/linux/bin/npidl"
}
```

Alternatively, if `npidl` is in your PATH, you can leave it as the default "npidl".

## Usage

### Open a .npidl file

Create a test file `test.npidl`:

```npidl
namespace Test {
  interface Calculator {
    i32 add(i32 a, i32 b);
    i32 multiply(i32 a, i32 b);
  };
  
  struct Result {
    i32 value;
    string message;
  };
}
```

### Features

Once the extension is active, you'll see:

1. **Syntax Highlighting** - Keywords, types, and comments are colored
2. **Error Diagnostics** - Syntax errors appear with red squiggles (coming soon)
3. **Hover Information** - Hover over symbols to see type info (coming soon)
4. **Go to Definition** - F12 or Ctrl+Click on symbols (coming soon)
5. **Document Outline** - View structure in the Outline panel (coming soon)

### Verify it's working

1. Open any `.npidl` file
2. Check the Output panel (View > Output)
3. Select "NPIDL Language Server" from the dropdown
4. You should see: "NPIDL LSP Server starting..."

## Troubleshooting

### Extension not activating

**Problem:** No syntax highlighting or LSP features

**Solution:**
- Verify the file extension is `.npidl`
- Check the Output panel for errors
- Restart VSCode (`Ctrl+Shift+P` > "Reload Window")

### LSP server not starting

**Problem:** No diagnostics or hover information

**Solution:**
- Verify npidl path in settings is correct
- Test the LSP server manually:
  ```bash
  /path/to/npidl --lsp
  ```
- Check that it prints: "NPIDL LSP Server starting..."
- Enable verbose logging in settings:
  ```json
  {
    "npidl.lsp.trace.server": "verbose"
  }
  ```

### Cannot find npidl executable

**Problem:** Error message about npidl not found

**Solution:**
- Update the path in VSCode settings to the full absolute path
- Or add npidl to your PATH:
  ```bash
  export PATH="$PATH:/path/to/npsystem/build/linux/bin"
  ```

### TypeScript compilation errors

**Problem:** Errors when running `npm run compile`

**Solution:**
- Delete `node_modules` and reinstall:
  ```bash
  rm -rf node_modules package-lock.json
  npm install
  npm run compile
  ```

## Development

### Watch mode for development

If you're working on the extension itself:

```bash
npm run watch
```

This automatically recompiles on file changes.

### Testing changes

1. Make changes to `src/extension.ts`
2. The watch task recompiles automatically
3. In the Extension Development Host window, press `Ctrl+R` to reload
4. Test your changes

### Adding features

See the main `LSP_README.md` for architecture details and how to add new LSP features.

## Next Steps

- Check out example `.npidl` files in the project
- Read `LSP_README.md` for implementation details
- Contribute new features (diagnostics, hover, go-to-definition)

## Support

- **LSP Issues:** See `LSP_README.md` troubleshooting section
- **Extension Issues:** Check the VSCode Output panel
- **Build Issues:** Verify npidl compiles with `--lsp` flag support

## Files Overview

```
vscode-extension/
├── src/
│   └── extension.ts          # Main extension code
├── syntaxes/
│   └── npidl.tmLanguage.json # Syntax highlighting rules
├── package.json              # Extension manifest
├── tsconfig.json             # TypeScript configuration
├── language-configuration.json # Bracket matching, comments, etc.
├── setup.sh                  # Quick setup script
└── README.md                 # Detailed documentation
```

That's it! You now have full LSP support for NPIDL in VSCode. 🎉
