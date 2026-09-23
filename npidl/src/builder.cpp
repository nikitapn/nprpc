// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "builder.hpp"
#include "ast.hpp"

namespace npidl::builders {

namespace {

template <typename Fn> void for_each_line(std::string_view text, Fn&& fn)
{
  while (true) {
    const auto nl = text.find('\n');
    fn(text.substr(0, nl));
    if (nl == std::string_view::npos)
      break;
    text.remove_prefix(nl + 1);
  }
}

// Continuation lines are indented so every consumer (Doxygen, DocC, TSDoc)
// keeps them inside the same parameter entry.
void append_indented(std::string& out, std::string_view text)
{
  bool first = true;
  for_each_line(text, [&](std::string_view line) {
    if (!first)
      out += "\n  ";
    out += line;
    first = false;
  });
}

} // namespace

void emit_line_doc(std::ostream& os, std::string_view indent, std::string_view doc)
{
  if (doc.empty())
    return;
  for_each_line(doc, [&](std::string_view line) {
    // In C++ a backslash at the end of a `//` comment splices the next line
    // of generated code into the comment. Markdown uses one for a hard line
    // break; losing that is the lesser evil.
    while (line.ends_with('\\'))
      line.remove_suffix(1);
    os << indent << "///";
    if (!line.empty())
      os << ' ' << line;
    os << '\n';
  });
}

void emit_block_doc(std::ostream& os, std::string_view indent, std::string_view doc)
{
  if (doc.empty())
    return;
  os << indent << "/**\n";
  for_each_line(doc, [&](std::string_view line) {
    os << indent << " *";
    if (!line.empty()) {
      os << ' ';
      // A literal `*/` in the text would close the comment early.
      for (size_t pos; (pos = line.find("*/")) != std::string_view::npos;) {
        os << line.substr(0, pos) << "*\\/";
        line.remove_prefix(pos + 2);
      }
      os << line;
    }
    os << '\n';
  });
  os << indent << " */\n";
}

std::string function_doc(const AstFunctionDecl* fn, ParamDocStyle style,
                         bool inputs_only)
{
  std::string out = fn->doc;
  for (const auto* arg : fn->args) {
    if (arg->doc.empty())
      continue;
    if (inputs_only && arg->modifier == ArgumentModifier::Out)
      continue;
    if (!out.empty())
      out += '\n';
    out += style == ParamDocStyle::Swift ? "- Parameter " : "@param ";
    out += arg->name;
    out += style == ParamDocStyle::Swift ? ": " : " ";
    append_indented(out, arg->doc);
  }

  // With out arguments dropped, a void function with exactly one of them
  // returns it, so its doc describes the return value.
  // Counted from args: out_args is filled by the argument-struct pass, which
  // need not have run yet.
  if (inputs_only && fn->is_void()) {
    const AstFunctionArgument* only_out = nullptr;
    size_t outs = 0;
    for (const auto* arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        only_out = arg;
        ++outs;
      }
    }
    if (outs == 1 && !only_out->doc.empty()) {
      if (!out.empty())
        out += '\n';
      out += style == ParamDocStyle::Swift ? "- Returns: " : "@returns ";
      append_indented(out, only_out->doc);
    }
  }
  return out;
}

void Builder::emit_arguments_structs(std::function<void(AstStructDecl*)> emitter)
{
  always_full_namespace(true);
  for (auto& [unused, s] : ctx_->affa_list)
    emitter(s);
  always_full_namespace(false);
}

void BuildGroup::generate_argument_structs(AstInterfaceDecl* ifs)
{
  for (auto& fn : ifs->fns)
    args_builder_.make_arguments_structs(fn);
}

} // namespace npidl::builders
