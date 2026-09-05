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
* [x] Fix reparsing after edits: `parse_for_lsp` now resets the context (AST pool + symbol table) and reloads builtins before every parse. `didChange` applies full or incremental edits, then reparses from scratch. Covered by `LspReparse` gtests and `test_reparse_after_edit.py`.

* [x] Function semantic tokens: the position index records the function *name* (`name_range`), not the whole signature, so parameters are separate tokens. Document symbols still use the full declaration range.

* [x] Fix AstNode* memory leak: AST nodes are allocated from `Context`'s `AstPool` and destroyed on `reset()` / `~Context()`. Namespaces own their children.

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
