# Dependency Injection Architecture for npidl

## Problem

Current design has dual constructors for Context-dependent functionality:
- **Lexer**: File-based vs in-memory
- **Parser**: Compiler mode vs LSP mode
- **Import Resolution**: Needs different behaviors for compiler vs LSP

This creates:
1. Conditional logic in constructors
2. Mode flags (`enable_recovery_`) scattered through code
3. Difficulty adding new modes or behaviors
4. Tight coupling between Parser and its dependencies

## Solution: Dependency Injection with Boost.DI

Use Boost.DI to inject the right dependencies based on execution mode.

### Dependencies to Abstract

```cpp
// 1. Source provider - where does source code come from?
class ISourceProvider {
public:
    virtual ~ISourceProvider() = default;
    virtual std::string read_file(const std::filesystem::path& path) = 0;
};

class FileSystemSourceProvider : public ISourceProvider {
    std::string read_file(const std::filesystem::path& path) override {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    }
};

class LspSourceProvider : public ISourceProvider {
    std::map<std::string, std::string> documents_;
    
public:
    void update_document(const std::string& uri, const std::string& content) {
        documents_[uri] = content;
    }
    
    std::string read_file(const std::filesystem::path& path) override {
        auto uri = path_to_uri(path);
        if (auto it = documents_.find(uri); it != documents_.end()) {
            return it->second;
        }
        // Fallback to filesystem for imported files not open in editor
        return FileSystemSourceProvider{}.read_file(path);
    }
};

// 2. Import resolver - how do we handle imports?
class IImportResolver {
public:
    virtual ~IImportResolver() = default;
    virtual std::optional<std::filesystem::path> resolve_import(
        const std::string& import_path,
        const std::filesystem::path& current_file_path
    ) = 0;
    virtual bool should_parse_import(const std::filesystem::path& resolved_path) = 0;
};

class CompilerImportResolver : public IImportResolver {
    std::unordered_set<std::filesystem::path> parsed_files_;
    
public:
    std::optional<std::filesystem::path> resolve_import(
        const std::string& import_path,
        const std::filesystem::path& current_file_path
    ) override {
        namespace fs = std::filesystem;
        
        // Resolve relative to current file
        auto base_dir = current_file_path.parent_path();
        auto resolved = fs::absolute(base_dir / import_path);
        
        if (!fs::exists(resolved)) {
            return std::nullopt;  // Error: file not found
        }
        
        return resolved;
    }
    
    bool should_parse_import(const std::filesystem::path& resolved_path) override {
        // Parse once per file (avoid circular imports)
        return parsed_files_.insert(resolved_path).second;
    }
};

class LspImportResolver : public IImportResolver {
    // LSP resolver doesn't need to track parsed files
    // (DocumentManager handles this at a higher level)
    
public:
    std::optional<std::filesystem::path> resolve_import(
        const std::string& import_path,
        const std::filesystem::path& current_file_path
    ) override {
        // Same resolution logic as compiler
        namespace fs = std::filesystem;
        auto base_dir = current_file_path.parent_path();
        auto resolved = fs::absolute(base_dir / import_path);
        
        // Don't check if file exists - LSP might have it in memory
        return resolved;
    }
    
    bool should_parse_import(const std::filesystem::path& resolved_path) override {
        // Always return true - LSP handles caching elsewhere
        // This prevents Parser from maintaining state
        return true;
    }
};

// 3. Error handler - what to do with errors?
class IErrorHandler {
public:
    virtual ~IErrorHandler() = default;
    virtual void handle_error(const parser_error& error) = 0;
    virtual bool should_continue_after_error() const = 0;
};

class CompilerErrorHandler : public IErrorHandler {
    void handle_error(const parser_error& error) override {
        // Throw immediately - stop compilation
        throw error;
    }
    
    bool should_continue_after_error() const override {
        return false;
    }
};

class LspErrorHandler : public IErrorHandler {
    std::vector<parser_error> errors_;
    
public:
    void handle_error(const parser_error& error) override {
        // Collect errors for later reporting
        errors_.push_back(error);
    }
    
    bool should_continue_after_error() const override {
        return true;  // Continue parsing to find all errors
    }
    
    const std::vector<parser_error>& get_errors() const {
        return errors_;
    }
};

// 4. Builder - what to generate?
// Already abstracted! BuildGroup vs NullBuilder
```

### Refactored Parser

```cpp
class Parser {
    Lexer& lex_;
    Context& ctx_;
    BuildGroup& builder_;
    IImportResolver& import_resolver_;
    IErrorHandler& error_handler_;
    
    // No more mode flags!
    // No more enable_recovery_ flag
    // No more errors_ vector (delegated to error_handler_)

public:
    // Single constructor - dependencies injected
    Parser(
        Lexer& lex,
        Context& ctx,
        BuildGroup& builder,
        IImportResolver& import_resolver,
        IErrorHandler& error_handler
    )
        : lex_(lex)
        , ctx_(ctx)
        , builder_(builder)
        , import_resolver_(import_resolver)
        , error_handler_(error_handler)
    {
    }
    
    void parse() {
        while (!done_) {
            try {
                if (!(
                    check(&Parser::stmt_decl) ||
                    check(&Parser::eof)
                )) {
                    throw_error("Expected tokens: statement, eof");
                }
            } catch (const parser_error& e) {
                error_handler_.handle_error(e);
                
                if (!error_handler_.should_continue_after_error()) {
                    throw;  // Re-throw for compiler mode
                }
                
                // LSP mode: synchronize and continue
                synchronize();
            }
        }
    }
    
    // Import handling uses injected resolver
    bool import_decl() {
        if (peek() != TokenId::Import) return false;
        
        auto import_tok = match(TokenId::Import);
        auto path_tok = match(TokenId::String);
        match(';');
        
        auto* import = new AstImportDecl();
        import->import_path = path_tok.name;
        import->import_line = import_tok.line;
        import->import_col = import_tok.col;
        // ... set other position fields ...
        
        // Use injected resolver
        auto resolved = import_resolver_.resolve_import(
            import->import_path,
            ctx_.get_file_path()
        );
        
        if (resolved) {
            import->resolved_path = *resolved;
            import->resolved = true;
            
            // Check if we should parse this import
            if (import_resolver_.should_parse_import(*resolved)) {
                FileContextGuard guard(ctx_, *resolved);
                
                // Read source using injected source provider
                // (stored in Lexer, which also uses DI)
                parse_imported_file(*resolved);
            }
        } else {
            import->resolved = false;
            import->error_message = "Cannot resolve import path";
            
            // Report error through error handler
            error_handler_.handle_error({
                .message = "Cannot resolve import: " + import->import_path,
                .line = import_tok.line,
                .column = import_tok.col
            });
        }
        
        ctx_.imports.push_back(import);
        return true;
    }
};
```

### Refactored Lexer

```cpp
class Lexer {
    ISourceProvider& source_provider_;
    Context& ctx_;
    
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    
public:
    // Single constructor - source provider injected
    Lexer(ISourceProvider& source_provider, Context& ctx)
        : source_provider_(source_provider)
        , ctx_(ctx)
    {
        // Load initial file
        source_ = source_provider_.read_file(ctx.get_file_path());
    }
    
    // Load a new file (for imports)
    void load_file(const std::filesystem::path& path) {
        source_ = source_provider_.read_file(path);
        pos_ = 0;
        line_ = 1;
        col_ = 1;
    }
    
    Token tok() {
        // ... existing tokenization logic ...
    }
};
```

### Boost.DI Configuration

```cpp
#include <boost/di.hpp>

namespace di = boost::di;

// Compiler mode injector
auto make_compiler_injector() {
    return di::make_injector(
        di::bind<ISourceProvider>().to<FileSystemSourceProvider>(),
        di::bind<IImportResolver>().to<CompilerImportResolver>(),
        di::bind<IErrorHandler>().to<CompilerErrorHandler>(),
        di::bind<BuildGroup>().to</* actual builder */>()
    );
}

// LSP mode injector
auto make_lsp_injector(LspSourceProvider& lsp_source) {
    return di::make_injector(
        di::bind<ISourceProvider>().to(lsp_source),  // Shared instance
        di::bind<IImportResolver>().to<LspImportResolver>(),
        di::bind<IErrorHandler>().to<LspErrorHandler>(),
        di::bind<BuildGroup>().to<NullBuilder>()
    );
}

// Usage in compiler
void compile_file(const std::filesystem::path& path) {
    auto injector = make_compiler_injector();
    
    Context ctx;
    ctx.open(path);
    
    auto parser = injector.create<Parser>();
    parser.parse();
    
    // If we got here, compilation succeeded
}

// Usage in LSP
class DocumentManager {
    LspSourceProvider source_provider_;
    std::map<std::string, std::unique_ptr<Context>> contexts_;
    
public:
    void parse_document(const std::string& uri, const std::string& content) {
        // Update source provider
        source_provider_.update_document(uri, content);
        
        // Create LSP injector
        auto injector = make_lsp_injector(source_provider_);
        
        // Parse
        auto ctx = std::make_unique<Context>();
        ctx->open(uri_to_path(uri));
        
        auto parser = injector.create<Parser>();
        parser.parse();
        
        // Store parsed context
        contexts_[uri] = std::move(ctx);
    }
};
```

## Benefits

### 1. **Single Responsibility**
- `Parser` focuses on parsing
- `Lexer` focuses on tokenization
- `ImportResolver` handles import logic
- `ErrorHandler` manages error reporting

### 2. **Open/Closed Principle**
- Add new modes without modifying existing code
- Example: Add "test mode" with mock source provider

### 3. **Testability**
```cpp
// Easy to mock dependencies for testing
class MockImportResolver : public IImportResolver {
    std::map<std::string, std::filesystem::path> mock_imports_;
    
public:
    void mock_import(const std::string& import_path, const std::filesystem::path& resolved) {
        mock_imports_[import_path] = resolved;
    }
    
    std::optional<std::filesystem::path> resolve_import(
        const std::string& import_path,
        const std::filesystem::path&
    ) override {
        if (auto it = mock_imports_.find(import_path); it != mock_imports_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    bool should_parse_import(const std::filesystem::path&) override {
        return true;  // Always parse in tests
    }
};

// Test import resolution
TEST(Parser, ImportResolution) {
    MockImportResolver resolver;
    resolver.mock_import("./types.npidl", "/project/types.npidl");
    
    LspErrorHandler error_handler;
    NullBuilder builder;
    
    // ... setup lexer, context ...
    
    Parser parser(lexer, ctx, builder, resolver, error_handler);
    parser.parse();
    
    ASSERT_EQ(ctx.imports.size(), 1);
    ASSERT_TRUE(ctx.imports[0]->resolved);
}
```

### 4. **No Mode Flags**
```cpp
// Before:
Parser parser(content, ctx, builder, /*enable_recovery=*/true);
parser.enable_recovery(true);  // Oops, where is this set?

// After:
auto injector = make_lsp_injector(source_provider);
auto parser = injector.create<Parser>();  // Dependencies determine behavior
```

### 5. **Easier to Extend**
```cpp
// Add "debug mode" without touching Parser
class DebugErrorHandler : public IErrorHandler {
    void handle_error(const parser_error& error) override {
        std::cerr << "[DEBUG] Error at " << error.line << ":" << error.column 
                  << " - " << error.message << std::endl;
        // Maybe set breakpoint here
    }
    
    bool should_continue_after_error() const override {
        return true;  // Continue debugging
    }
};

auto make_debug_injector() {
    return di::make_injector(
        di::bind<ISourceProvider>().to<FileSystemSourceProvider>(),
        di::bind<IImportResolver>().to<CompilerImportResolver>(),
        di::bind<IErrorHandler>().to<DebugErrorHandler>(),  // New!
        di::bind<BuildGroup>().to<NullBuilder>()
    );
}
```

## Migration Plan

### Phase 1: Define Interfaces (1-2 hours)
- Create `ISourceProvider`, `IImportResolver`, `IErrorHandler`
- Add to `ast.hpp` or new `interfaces.hpp`

### Phase 2: Implement Concrete Classes (2-3 hours)
- `FileSystemSourceProvider`, `LspSourceProvider`
- `CompilerImportResolver`, `LspImportResolver`
- `CompilerErrorHandler`, `LspErrorHandler`

### Phase 3: Refactor Lexer (1 hour)
- Remove dual constructors
- Inject `ISourceProvider`
- Update `load_file()` to use source provider

### Phase 4: Refactor Parser (2-3 hours)
- Remove dual constructors
- Remove `enable_recovery_` flag
- Remove `errors_` vector (use `IErrorHandler`)
- Inject dependencies
- Update `import_decl()` to use `IImportResolver`
- Update error handling to use `IErrorHandler`

### Phase 5: Setup Boost.DI (1 hour)
- Add Boost.DI to CMakeLists.txt (header-only)
- Create `make_compiler_injector()`, `make_lsp_injector()`
- Update `compile()` function
- Update LSP server `DocumentManager`

### Phase 6: Update Tests (1-2 hours)
- Create mock implementations
- Update existing tests to use DI
- Add new tests for import resolution

### Total Time: ~8-12 hours (1-2 days)

## Alternative: Manual DI (Without Boost.DI)

If you don't want to add Boost.DI dependency:

```cpp
// Simple factory functions
struct ParserDependencies {
    ISourceProvider& source_provider;
    IImportResolver& import_resolver;
    IErrorHandler& error_handler;
    BuildGroup& builder;
};

// Compiler factory
ParserDependencies make_compiler_dependencies(BuildGroup& builder) {
    static FileSystemSourceProvider source;
    static CompilerImportResolver resolver;
    static CompilerErrorHandler error_handler;
    
    return ParserDependencies{
        .source_provider = source,
        .import_resolver = resolver,
        .error_handler = error_handler,
        .builder = builder
    };
}

// LSP factory
ParserDependencies make_lsp_dependencies(LspSourceProvider& source) {
    static LspImportResolver resolver;
    static LspErrorHandler error_handler;
    static NullBuilder builder;
    
    return ParserDependencies{
        .source_provider = source,
        .import_resolver = resolver,
        .error_handler = error_handler,
        .builder = builder
    };
}

// Usage:
auto deps = make_compiler_dependencies(builder);
Lexer lexer(deps.source_provider, ctx);
Parser parser(lexer, ctx, deps.builder, deps.import_resolver, deps.error_handler);
```

This achieves the same benefits without adding a dependency, but requires manual lifetime management.

## Recommendation

**Use Boost.DI** for:
- Cleaner dependency injection
- Automatic lifetime management
- Better testability
- Future extensibility

**Use manual DI** if:
- Want to minimize dependencies
- Simple use case (only 2 modes: compiler vs LSP)
- Concerned about compilation time

Given that you already use Boost (Boost.ProgramOptions), adding Boost.DI (header-only) is a minimal cost for significant architectural improvement.
