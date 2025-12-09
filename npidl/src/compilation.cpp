#include "builder.hpp"
#include "cpp_builder.hpp"
#include "parser_factory.hpp"
#include "ts_builder.hpp"

// Implementation of ICompilation and CompilationBuilder
namespace npidl {

struct Compilation {
  const builders::BuildGroup build_group_;
  const std::vector<std::filesystem::path> input_files_;

  std::vector<parser_error> errors_;

  Compilation(builders::BuildGroup&& build_group,
              std::vector<std::filesystem::path>&& input_files)
      : build_group_(std::move(build_group)),
        input_files_(std::move(input_files))
  {
  }

  void compile()
  {
    for (const auto& file : input_files_) {
      Context ctx(file);
      builders::BuildGroup build_group(build_group_, &ctx);

      // Use factory to create parser with dependencies
      auto [source_provider, import_resolver, error_handler, lexer, parser] =
          ParserFactory::create_compiler_parser(ctx, build_group);

      parser->parse();
      build_group.finalize();
    }
  }

  const std::vector<parser_error>& get_errors() const
  {
    return errors_;
    // return ctx.get_errors();
  }

  bool has_errors() const
  {
    return false;
    // return ctx.has_errors();
  }
};

ICompilation::~ICompilation() = default;

void ICompilation::compile() { impl_->compile(); }

const std::vector<parser_error>& ICompilation::get_errors() const
{
  return impl_->get_errors();
}

bool ICompilation::has_errors() const { return impl_->has_errors(); }

CompilationBuilder& CompilationBuilder::set_input_files(
    const std::vector<std::filesystem::path>& files)
{
  input_files_ = files;
  return *this;
}

CompilationBuilder&
CompilationBuilder::set_output_dir(const std::filesystem::path& output_dir)
{
  output_dir_ = output_dir;
  return *this;
}

CompilationBuilder& CompilationBuilder::with_language_cpp()
{
  language_flags_ |= LanguageFlags::Cpp;
  return *this;
}

CompilationBuilder& CompilationBuilder::with_language_ts()
{
  language_flags_ |= LanguageFlags::TypeScript;
  return *this;
}

std::unique_ptr<ICompilation> CompilationBuilder::build()
{
  builders::BuildGroup builder;
  if (language_flags_ & LanguageFlags::Cpp)
    builder.add<builders::CppBuilder>(output_dir_);
  if (language_flags_ & LanguageFlags::TypeScript)
    builder.add<builders::TSBuilder>(output_dir_);

  auto compilation = std::make_unique<ICompilation>();
  compilation->impl_ = std::make_unique<Compilation>(std::move(builder),
                                                     std::move(input_files_));

  return compilation;
}
} // namespace npidl