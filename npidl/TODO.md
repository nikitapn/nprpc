# Things that need to be done

## Completed ✅
* [x] **LSP Server Implementation** - Complete working LSP server with:
  - textDocument/hover with type information
  - textDocument/definition (go-to-definition for types, aliases, parameters, type references)
  - textDocument/semanticTokens/full for syntax highlighting
  - textDocument/publishDiagnostics for error reporting with accurate token ranges
  - textDocument/documentSymbol for document outline
  - Position index for fast AST node lookup
  - Type reference tracking separate from type definitions
  - VS Code extension integration
  - Emacs integration (npidl-mode.el)
  - Integration test suite (7 tests covering main LSP features)

## Urgent / High Priority
* [ ] Fix reparsing after edits: currently the same context remains after an edit with the old AST and the symbol table, leading to immediate error during reparse. Need to invalidate context on edit and reparse from scratch. Or implement incremental reparsing properly.

* [ ] Some weirdness with functions: they are counted as one semantic token, but have multiple children for parameters. Need to verify if this is correct behavior or if functions should be expanded differently. Go-to-definition on function parameters is working, though.

* [ ] Fix AstNode* memory leak: currently AstNode objects are allocated with `new` but never deleted, leading to memory leaks. Implement proper memory management, possibly using smart pointers or an arena allocator to manage AST node lifetimes effectively.

## Medium Priority
* [ ] Add more LSP features: implement additional LSP capabilities such as:
  - textDocument/references (find all usages)
  - workspace/symbol (global symbol search)
  - textDocument/completion (autocomplete)
  - textDocument/signatureHelp (function signature hints)
  - textDocument/rename (rename symbol)
  - textDocument/codeAction (quick fixes, refactoring)
  - textDocument/formatting (code formatting)

* [ ] Cross-file navigation: handle imports and jump to definitions in other files

* [ ] Write more integration tests: increase test coverage by adding tests for edge cases and complex scenarios in the LSP server.

## Low Priority / Easy
* [x] Make `in` modifier optional and assume `in` by default for function parameters.

* [x] Change `flat` to `message` for structs and `using` to `alias` for type aliases to match common IDL terminology.

* [x] Improve error messages: enhance the clarity and helpfulness of parser and semantic error messages.
