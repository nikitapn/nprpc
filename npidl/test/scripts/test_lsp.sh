#!/bin/bash

# Test LSP server initialization
printf 'Content-Length: 107\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}' | ./build/linux/bin/npidl --lsp
