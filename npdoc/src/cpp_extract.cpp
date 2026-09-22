// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "cpp_extract.hpp"
#include "doc_text.hpp"

#include <clang-c/Index.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace npdoc {

namespace {

std::string take(CXString s)
{
  const char* c = clang_getCString(s);
  std::string out = c ? c : "";
  clang_disposeString(s);
  return out;
}

struct IndexDeleter {
  void operator()(void* p) const { clang_disposeIndex(p); }
};
struct TuDeleter {
  void operator()(CXTranslationUnitImpl* p) const
  {
    clang_disposeTranslationUnit(p);
  }
};
struct PolicyDeleter {
  void operator()(void* p) const { clang_PrintingPolicy_dispose(p); }
};

bool is_record_kind(CXCursorKind k)
{
  return k == CXCursor_StructDecl || k == CXCursor_ClassDecl ||
         k == CXCursor_UnionDecl || k == CXCursor_ClassTemplate;
}

bool is_function_kind(CXCursorKind k)
{
  return k == CXCursor_FunctionDecl || k == CXCursor_CXXMethod ||
         k == CXCursor_Constructor || k == CXCursor_ConversionFunction ||
         k == CXCursor_FunctionTemplate;
}

class Extractor
{
  const CppOptions& opt_;
  std::vector<fs::path> roots_; // canonical
  CXPrintingPolicy policy_ = nullptr;
  // Keyed by id; a later declaration of the same entity replaces an earlier
  // one only if it brings a doc the first lacked.
  std::map<std::string, Symbol> symbols_;
  std::unordered_map<CXFile, std::optional<fs::path>> file_cache_;

public:
  explicit Extractor(const CppOptions& opt)
      : opt_(opt)
  {
    for (const auto& r : opt.header_roots)
      roots_.push_back(fs::weakly_canonical(r));
  }

  void set_policy(CXPrintingPolicy p) { policy_ = p; }

  std::vector<Symbol> take_symbols()
  {
    std::vector<Symbol> out;
    out.reserve(symbols_.size());
    for (auto& [id, s] : symbols_)
      out.push_back(std::move(s));
    return out;
  }

  // Path of the header a cursor is spelled in, relative to opt_.root, if it
  // is one of ours.
  std::optional<fs::path> our_file(CXCursor c, unsigned* line)
  {
    CXFile file = nullptr;
    clang_getSpellingLocation(clang_getCursorLocation(c), &file, line, nullptr,
                              nullptr);
    if (!file)
      return std::nullopt;
    if (auto it = file_cache_.find(file); it != file_cache_.end())
      return it->second;

    std::optional<fs::path> result;
    auto real = take(clang_File_tryGetRealPathName(file));
    if (real.empty())
      real = take(clang_getFileName(file));
    const auto path = fs::weakly_canonical(real);
    for (const auto& root : roots_) {
      const auto rel = path.lexically_relative(root);
      if (rel.empty() || *rel.begin() == "..")
        continue;
      const auto rel_str = rel.generic_string();
      const bool excluded = std::any_of(
          opt_.exclude_prefixes.begin(), opt_.exclude_prefixes.end(),
          [&](const std::string& p) { return rel_str.starts_with(p); });
      if (!excluded)
        result = path.lexically_relative(fs::weakly_canonical(opt_.root));
      break;
    }
    file_cache_.emplace(file, result);
    return result;
  }

  static std::string qualified_name(CXCursor c)
  {
    std::vector<std::string> parts;
    for (auto cur = c; !clang_Cursor_isNull(cur) &&
                       clang_getCursorKind(cur) != CXCursor_TranslationUnit;
         cur = clang_getCursorSemanticParent(cur)) {
      parts.push_back(take(clang_getCursorSpelling(cur)));
    }
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
      if (!out.empty())
        out += "::";
      out += *it;
    }
    return out;
  }

  static std::string id_of(CXCursor c)
  {
    const auto kind = clang_getCursorKind(c);
    std::string id = "cpp:" + qualified_name(c);
    if (is_function_kind(kind)) {
      // Parameter types keep overloads apart: display name is `f(int, T)`.
      const auto display = take(clang_getCursorDisplayName(c));
      if (const auto paren = display.find('('); paren != std::string::npos)
        id += display.substr(paren);
      if (clang_CXXMethod_isConst(c))
        id += " const";
    }
    return id;
  }

  std::string signature_of(CXCursor c, CXCursorKind kind)
  {
    auto sig = take(clang_getCursorPrettyPrinted(c, policy_));
    // Records and enums print with an (empty, under TerseOutput) body.
    if (is_record_kind(kind) || kind == CXCursor_EnumDecl) {
      if (const auto brace = sig.rfind('{'); brace != std::string::npos)
        sig.erase(brace);
    }
    while (!sig.empty() && std::isspace(static_cast<unsigned char>(sig.back())))
      sig.pop_back();
    return sig;
  }

  static std::string kind_name(CXCursor c, CXCursorKind kind)
  {
    switch (kind) {
    case CXCursor_StructDecl: return "struct";
    case CXCursor_ClassDecl: return "class";
    case CXCursor_UnionDecl: return "union";
    case CXCursor_ClassTemplate:
      return clang_getTemplateCursorKind(c) == CXCursor_StructDecl ? "struct"
                                                                    : "class";
    case CXCursor_EnumDecl: return "enum";
    case CXCursor_EnumConstantDecl: return "case";
    case CXCursor_Constructor: return "constructor";
    case CXCursor_FieldDecl: return "field";
    case CXCursor_VarDecl: return "variable";
    case CXCursor_TypedefDecl:
    case CXCursor_TypeAliasDecl:
    case CXCursor_TypeAliasTemplateDecl: return "typealias";
    case CXCursor_ConceptDecl: return "concept";
    default: break;
    }
    if (is_function_kind(kind)) {
      if (take(clang_getCursorSpelling(c)).starts_with("operator"))
        return "operator";
      const auto parent_kind =
          clang_getCursorKind(clang_getCursorSemanticParent(c));
      return is_record_kind(parent_kind) ? "method" : "function";
    }
    return {};
  }

  static std::vector<std::pair<std::string, std::string>> params_of(CXCursor c)
  {
    std::vector<std::pair<std::string, std::string>> out;
    const int n = clang_Cursor_getNumArguments(c);
    if (n >= 0) {
      for (int i = 0; i < n; ++i) {
        const auto arg = clang_Cursor_getArgument(c, static_cast<unsigned>(i));
        out.emplace_back(take(clang_getCursorSpelling(arg)),
                         take(clang_getTypeSpelling(clang_getCursorType(arg))));
      }
      return out;
    }
    // Function templates do not report arguments; their ParmDecl children do.
    clang_visitChildren(
        c,
        [](CXCursor child, CXCursor, CXClientData data) {
          if (clang_getCursorKind(child) == CXCursor_ParmDecl) {
            static_cast<decltype(out)*>(data)->emplace_back(
                take(clang_getCursorSpelling(child)),
                take(clang_getTypeSpelling(clang_getCursorType(child))));
          }
          return CXChildVisit_Continue;
        },
        &out);
    return out;
  }

  void add(CXCursor c, CXCursorKind kind, const fs::path& file, unsigned line)
  {
    Symbol s;
    s.lang = "cpp";
    s.kind = kind_name(c, kind);
    s.name = take(clang_getCursorSpelling(c));
    // Conversion operators spell their type canonically
    // (`type-parameter-0-0`); the written result type reads as the source.
    if (kind == CXCursor_ConversionFunction)
      s.name = "operator " + take(clang_getTypeSpelling(clang_getCursorResultType(c)));
    s.qualified = qualified_name(c);
    if (kind == CXCursor_ConversionFunction)
      s.qualified =
          qualified_name(clang_getCursorSemanticParent(c)) + "::" + s.name;
    s.id = id_of(c);
    s.signature = signature_of(c, kind);
    s.file = file.generic_string();
    s.line = static_cast<int>(line);

    const auto parent = clang_getCursorSemanticParent(c);
    const auto parent_kind = clang_getCursorKind(parent);
    if (is_record_kind(parent_kind) || parent_kind == CXCursor_EnumDecl)
      s.parent = id_of(parent);

    auto parts = parse_doxygen(
        strip_comment_markers(take(clang_Cursor_getRawCommentText(c))));
    s.doc = std::move(parts.doc);
    s.summary = summary_of(s.doc);
    s.returns = std::move(parts.returns);

    if (is_function_kind(kind)) {
      std::vector<Param> params;
      for (auto& [name, type] : params_of(c)) {
        Param p{name, type, {}, {}};
        if (auto it = std::find_if(parts.params.begin(), parts.params.end(),
                                   [&](const Param& d) { return d.name == name; });
            it != parts.params.end()) {
          p.doc = std::move(it->doc);
          parts.params.erase(it);
        }
        params.push_back(std::move(p));
      }
      // @param entries naming no real parameter: keep the text, since a
      // renamed parameter is more likely than a doc worth dropping.
      for (auto& stray : parts.params)
        params.push_back(std::move(stray));
      if (!params.empty())
        s.params = std::move(params);
    }

    auto [it, inserted] = symbols_.try_emplace(s.id, s);
    if (!inserted && it->second.doc.empty() && !s.doc.empty())
      it->second = std::move(s);
  }

  static bool internal_name(std::string_view name)
  {
    return name.empty() || name.starts_with('_') ||
           name.find("(unnamed") != std::string_view::npos ||
           name.find("(anonymous") != std::string_view::npos;
  }

  CXChildVisitResult visit(CXCursor c)
  {
    const auto kind = clang_getCursorKind(c);

    if (kind == CXCursor_Namespace) {
      const auto name = take(clang_getCursorSpelling(c));
      if (clang_Cursor_isAnonymous(c) || name == "detail" || name == "impl" ||
          internal_name(name))
        return CXChildVisit_Continue;
      return CXChildVisit_Recurse;
    }
    if (kind == CXCursor_LinkageSpec)
      return CXChildVisit_Recurse;

    const bool interesting =
        is_record_kind(kind) || is_function_kind(kind) ||
        kind == CXCursor_EnumDecl || kind == CXCursor_EnumConstantDecl ||
        kind == CXCursor_FieldDecl || kind == CXCursor_VarDecl ||
        kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl ||
        kind == CXCursor_TypeAliasTemplateDecl || kind == CXCursor_ConceptDecl;
    if (!interesting)
      return CXChildVisit_Continue;

    // Members: public only. Non-members report an invalid specifier.
    const auto access = clang_getCXXAccessSpecifier(c);
    if (access == CX_CXXPrivate || access == CX_CXXProtected)
      return CXChildVisit_Continue;

    if (internal_name(take(clang_getCursorSpelling(c))))
      return CXChildVisit_Continue;

    unsigned line = 0;
    const auto file = our_file(c, &line);
    if (!file)
      return CXChildVisit_Continue;

    if (is_record_kind(kind) || kind == CXCursor_EnumDecl) {
      // Forward declarations would duplicate the definition's entry.
      if (!clang_isCursorDefinition(c))
        return CXChildVisit_Continue;
      add(c, kind, *file, line);
      // A coroutine's promise_type is listed, but its members are the
      // language's protocol (initial_suspend, yield_value, ...), not API.
      if (take(clang_getCursorSpelling(c)) == "promise_type")
        return CXChildVisit_Continue;
      return CXChildVisit_Recurse;
    }

    if (is_function_kind(kind)) {
      // An out-of-line member definition repeats the in-class declaration,
      // which is where the doc lives.
      if (!clang_equalCursors(clang_getCursorLexicalParent(c),
                              clang_getCursorSemanticParent(c)))
        return CXChildVisit_Continue;
      if (clang_CXXMethod_isDeleted(c))
        return CXChildVisit_Continue;
      if (clang_CXXMethod_isDefaulted(c) &&
          take(clang_Cursor_getRawCommentText(c)).empty())
        return CXChildVisit_Continue;
    }

    // Only namespace-scope and static member variables; locals never get
    // here because function bodies are skipped.
    add(c, kind, *file, line);
    return CXChildVisit_Continue;
  }
};

} // namespace

std::vector<Symbol> extract_cpp(const CppOptions& options)
{
  // One translation unit that includes every header, so each header is
  // parsed once with the same configuration.
  std::string umbrella;
  for (const auto& root : options.header_roots) {
    std::vector<fs::path> headers;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file())
        continue;
      const auto ext = entry.path().extension();
      if (ext != ".hpp" && ext != ".h")
        continue;
      const auto rel = entry.path().lexically_relative(root).generic_string();
      if (std::any_of(options.exclude_prefixes.begin(),
                      options.exclude_prefixes.end(),
                      [&](const std::string& p) { return rel.starts_with(p); }))
        continue;
      headers.push_back(fs::absolute(entry.path()));
    }
    std::sort(headers.begin(), headers.end());
    for (const auto& h : headers)
      umbrella += "#include \"" + h.string() + "\"\n";
  }

  std::vector<const char*> args{"-x", "c++"};
  for (const auto& a : options.clang_args)
    args.push_back(a.c_str());

  const char* const umbrella_name = "npdoc_umbrella.cpp";
  CXUnsavedFile unsaved{umbrella_name, umbrella.c_str(),
                        static_cast<unsigned long>(umbrella.size())};

  std::unique_ptr<void, IndexDeleter> index(clang_createIndex(0, 0));
  CXTranslationUnit raw_tu = nullptr;
  const auto err = clang_parseTranslationUnit2(
      index.get(), umbrella_name, args.data(), static_cast<int>(args.size()),
      &unsaved, 1,
      CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_KeepGoing,
      &raw_tu);
  if (err != CXError_Success || !raw_tu)
    throw std::runtime_error("libclang could not parse the headers (error " +
                             std::to_string(static_cast<int>(err)) + ")");
  std::unique_ptr<CXTranslationUnitImpl, TuDeleter> tu(raw_tu);

  // Errors mean some declarations may be missing or misprinted; say so
  // rather than silently publishing a partial API.
  const unsigned ndiag = clang_getNumDiagnostics(tu.get());
  unsigned errors = 0;
  for (unsigned i = 0; i < ndiag; ++i) {
    CXDiagnostic d = clang_getDiagnostic(tu.get(), i);
    if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) {
      if (errors++ < 10)
        std::cerr << "npdoc: "
                  << take(clang_formatDiagnostic(
                         d, clang_defaultDiagnosticDisplayOptions()))
                  << '\n';
    }
    clang_disposeDiagnostic(d);
  }
  if (errors)
    std::cerr << "npdoc: warning: " << errors
              << " error(s) while parsing C++ headers; output may be incomplete\n";

  const auto tu_cursor = clang_getTranslationUnitCursor(tu.get());
  std::unique_ptr<void, PolicyDeleter> policy(
      clang_getCursorPrintingPolicy(tu_cursor));
  clang_PrintingPolicy_setProperty(policy.get(), CXPrintingPolicy_TerseOutput, 1);
  clang_PrintingPolicy_setProperty(policy.get(),
                                   CXPrintingPolicy_PolishForDeclaration, 1);

  Extractor ex(options);
  ex.set_policy(policy.get());
  clang_visitChildren(
      tu_cursor,
      [](CXCursor c, CXCursor, CXClientData data) {
        return static_cast<Extractor*>(data)->visit(c);
      },
      &ex);
  return ex.take_symbols();
}

} // namespace npdoc
