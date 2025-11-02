#!/bin/bash

# Setup script for NPIDL VSCode extension

set -e

echo "Setting up NPIDL VSCode extension..."

# Navigate to extension directory
cd "$(dirname "$0")"

# Check if npm is installed
if ! command -v npm &> /dev/null; then
    echo "Error: npm is not installed. Please install Node.js and npm first."
    exit 1
fi

# Install dependencies
echo "Installing dependencies..."
npm install

# Compile TypeScript
echo "Compiling TypeScript..."
npm run compile

echo ""
echo "✅ Extension compiled successfully!"
echo ""
echo "To install the extension in VSCode:"
echo "1. Package the extension:"
echo "   npm install -g @vscode/vsce"
echo "   vsce package"
echo ""
echo "2. Install the generated .vsix file:"
echo "   code --install-extension npidl-lsp-*.vsix"
echo ""
echo "Or for development:"
echo "1. Open this folder in VSCode"
echo "2. Press F5 to launch Extension Development Host"
echo "3. Open a .npidl file in the new window"
echo ""
echo "Don't forget to configure the npidl path in VSCode settings:"
echo "   \"npidl.lsp.path\": \"$(pwd)/../../build/linux/bin/npidl\""
