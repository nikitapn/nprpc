// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include "parser_implementations.hpp"
#include <memory>

namespace npidl {

// Forward declarations
class Lexer;
class Parser;
class Context;
class BuildGroup;

// Factory for creating parsers with dependencies
class ParserFactory {
public:
    // Create parser for compiler mode (file-based, throws on error)
    static std::tuple<
        std::unique_ptr<FileSystemSourceProvider>,
        std::unique_ptr<CompilerImportResolver>,
        std::unique_ptr<CompilerErrorHandler>,
        std::unique_ptr<Lexer>,
        std::unique_ptr<Parser>
    >
    create_compiler_parser(Context& ctx, BuildGroup& builder);
    
    // Create parser for LSP mode (in-memory, collects errors)
    static std::tuple<
        std::shared_ptr<LspSourceProvider>,      // Shared across documents
        std::unique_ptr<LspImportResolver>,
        std::unique_ptr<LspErrorHandler>,
        std::unique_ptr<Lexer>,
        std::unique_ptr<Parser>
    >
    create_lsp_parser(Context& ctx, BuildGroup& builder, std::shared_ptr<LspSourceProvider> source_provider = nullptr);
};

} // namespace npidl
