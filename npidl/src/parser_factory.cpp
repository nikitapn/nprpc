// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "parser_factory.hpp"
#include "ast.hpp"
#include "builder.hpp"

// Need to declare Lexer and Parser classes here since they're defined in library.cpp
// We'll forward declare and let the compiler work it out
namespace npidl {
    class Lexer;
    class Parser;
}

// Include the actual definitions
#include "library.cpp"

namespace npidl {

std::tuple<
    std::unique_ptr<FileSystemSourceProvider>,
    std::unique_ptr<CompilerImportResolver>,
    std::unique_ptr<CompilerErrorHandler>,
    std::unique_ptr<Lexer>,
    std::unique_ptr<Parser>
>
ParserFactory::create_compiler_parser(Context& ctx, BuildGroup& builder) {
    auto source_provider = std::make_unique<FileSystemSourceProvider>();
    auto import_resolver = std::make_unique<CompilerImportResolver>();
    auto error_handler = std::make_unique<CompilerErrorHandler>();
    
    auto lexer = std::make_unique<Lexer>(*source_provider, ctx);
    auto parser = std::make_unique<Parser>(
        *lexer,
        ctx,
        builder,
        *import_resolver,
        *error_handler
    );
    
    return {
        std::move(source_provider),
        std::move(import_resolver),
        std::move(error_handler),
        std::move(lexer),
        std::move(parser)
    };
}

std::tuple<
    std::shared_ptr<LspSourceProvider>,
    std::unique_ptr<LspImportResolver>,
    std::unique_ptr<LspErrorHandler>,
    std::unique_ptr<Lexer>,
    std::unique_ptr<Parser>
>
ParserFactory::create_lsp_parser(Context& ctx, BuildGroup& builder, std::shared_ptr<LspSourceProvider> source_provider) {
    if (!source_provider) {
        source_provider = std::make_shared<LspSourceProvider>();
    }
    
    auto import_resolver = std::make_unique<LspImportResolver>();
    auto error_handler = std::make_unique<LspErrorHandler>();
    
    auto lexer = std::make_unique<Lexer>(*source_provider, ctx);
    auto parser = std::make_unique<Parser>(
        *lexer,
        ctx,
        builder,
        *import_resolver,
        *error_handler
    );
    
    return {
        source_provider,
        std::move(import_resolver),
        std::move(error_handler),
        std::move(lexer),
        std::move(parser)
    };
}

} // namespace npidl
