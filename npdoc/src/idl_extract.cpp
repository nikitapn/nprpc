// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "idl_extract.hpp"
#include "doc_text.hpp"
#include "json_io.hpp"

namespace fs = std::filesystem;

namespace npdoc {

namespace idl {

// Mirrors npidl's DocJsonBuilder output (format 1).
struct Field {
  std::string name;
  std::string type;
  int line = 0;
  std::string doc;
};
struct Item {
  std::string name;
  std::int64_t value = 0;
  std::string doc;
};
struct Arm {
  std::string name;
  std::string type;
};
struct Param {
  std::string name;
  std::string direction;
  bool direct = false;
  std::string type;
  std::string doc;
};
struct Method {
  std::string name;
  int line = 0;
  std::string doc;
  std::string returns;
  std::string stream;
  bool unreliable = false;
  std::vector<Param> params;
  std::vector<std::string> raises;
};
struct Decl {
  std::string kind;
  std::string name;
  std::string namespace_;
  std::string file;
  int line = 0;
  std::string doc;
  std::vector<Field> fields;
  std::string underlying;
  std::vector<Item> items;
  std::string target;
  std::vector<Arm> arms;
  bool trusted = false;
  std::vector<std::string> bases;
  std::vector<Method> methods;
  std::string value;
};
struct File {
  int format = 0;
  std::string file;
  std::string module;
  std::vector<Decl> declarations;
};

} // namespace idl

} // namespace npdoc

template <> struct glz::meta<npdoc::idl::Decl> {
  using T = npdoc::idl::Decl;
  // `namespace` is a keyword in C++.
  static constexpr auto value = object(
      "kind", &T::kind, "name", &T::name, "namespace", &T::namespace_, "file",
      &T::file, "line", &T::line, "doc", &T::doc, "fields", &T::fields,
      "underlying", &T::underlying, "items", &T::items, "target", &T::target,
      "arms", &T::arms, "trusted", &T::trusted, "bases", &T::bases, "methods",
      &T::methods, "value", &T::value);
};

namespace npdoc {

namespace {

// Fields store optionality in the type (`string?`); IDL puts it on the name.
std::string field_signature(const idl::Field& f)
{
  if (f.type.ends_with('?'))
    return f.name + "?: " + f.type.substr(0, f.type.size() - 1);
  return f.name + ": " + f.type;
}

std::string method_signature(const idl::Method& m)
{
  std::string sig;
  if (m.unreliable)
    sig += "[unreliable] ";
  sig += m.returns + " " + m.name + "(";
  for (size_t i = 0; i < m.params.size(); ++i) {
    const auto& p = m.params[i];
    if (i)
      sig += ", ";
    sig += p.name + ": ";
    if (p.direction == "out")
      sig += p.direct ? "out direct " : "out ";
    sig += p.type;
  }
  sig += ")";
  if (!m.raises.empty()) {
    sig += " raises(";
    for (size_t i = 0; i < m.raises.size(); ++i)
      sig += (i ? ", " : "") + m.raises[i];
    sig += ")";
  }
  return sig;
}

Symbol make(std::string kind, std::string name, std::string qualified,
            std::string signature, std::string doc,
            std::optional<std::string> parent, const std::string& file,
            int line)
{
  Symbol s;
  s.lang = "idl";
  s.id = "idl:" + qualified;
  s.kind = std::move(kind);
  s.name = std::move(name);
  s.qualified = std::move(qualified);
  s.signature = std::move(signature);
  s.doc = std::move(doc);
  s.summary = summary_of(s.doc);
  s.parent = std::move(parent);
  if (!file.empty())
    s.file = file;
  if (line > 0)
    s.line = line;
  return s;
}

} // namespace

std::vector<Symbol> extract_idl(const fs::path& doc_json, const fs::path& root)
{
  idl::File in;
  read_json_file(doc_json, in);
  if (in.format != 1)
    throw std::runtime_error(doc_json.string() + ": unsupported format " +
                             std::to_string(in.format));

  const auto base = fs::weakly_canonical(root);
  std::vector<Symbol> out;
  for (const auto& d : in.declarations) {
    // Same rule as for C++: `detail` and `impl` namespaces are internal (the
    // wire protocol, object ids). Their docs still reach the generated code.
    bool internal = false;
    for (std::string_view ns = d.namespace_; !ns.empty();) {
      const auto dot = ns.find('.');
      const auto part = ns.substr(0, dot);
      internal |= part == "detail" || part == "impl";
      ns = dot == std::string_view::npos ? std::string_view{} : ns.substr(dot + 1);
    }
    if (internal)
      continue;

    const auto qualified =
        d.namespace_.empty() ? d.name : d.namespace_ + "." + d.name;
    const auto file =
        d.file.empty() ? std::string()
                       : fs::path(d.file).lexically_relative(base).generic_string();
    const auto self = "idl:" + qualified;

    if (d.kind == "message" || d.kind == "exception") {
      out.push_back(make(d.kind, d.name, qualified, d.kind + " " + d.name,
                         d.doc, {}, file, d.line));
      for (const auto& f : d.fields)
        out.push_back(make("field", f.name, qualified + "." + f.name,
                           field_signature(f), f.doc, self, file, f.line));
    } else if (d.kind == "enum") {
      out.push_back(make("enum", d.name, qualified,
                         "enum " + d.name + " : " + d.underlying, d.doc, {},
                         file, d.line));
      for (const auto& item : d.items)
        out.push_back(make("case", item.name, qualified + "." + item.name,
                           item.name + " = " + std::to_string(item.value),
                           item.doc, self, file, 0));
    } else if (d.kind == "alias") {
      out.push_back(make("typealias", d.name, qualified,
                         "alias " + d.name + " = " + d.target, d.doc, {}, file,
                         d.line));
    } else if (d.kind == "variant") {
      std::string sig = "alias " + d.name + " = one of { ";
      for (const auto& arm : d.arms)
        sig += arm.name + ": " + arm.type + "; ";
      sig += "}";
      out.push_back(
          make("variant", d.name, qualified, sig, d.doc, {}, file, d.line));
    } else if (d.kind == "interface") {
      std::string sig = d.trusted ? "[trusted] interface " : "interface ";
      sig += d.name;
      for (size_t i = 0; i < d.bases.size(); ++i)
        sig += (i ? ", " : " : ") + d.bases[i];
      out.push_back(
          make("interface", d.name, qualified, sig, d.doc, {}, file, d.line));
      for (const auto& m : d.methods) {
        auto s = make("method", m.name, qualified + "." + m.name,
                      method_signature(m), m.doc, self, file, m.line);
        std::vector<Param> params;
        for (const auto& p : m.params)
          params.push_back({p.name, p.type, p.direction, p.doc});
        if (!params.empty())
          s.params = std::move(params);
        out.push_back(std::move(s));
      }
    } else if (d.kind == "const") {
      out.push_back(make("constant", d.name, qualified,
                         "const " + d.name + " = " + d.value, d.doc, {}, file,
                         d.line));
    }
  }
  return out;
}

} // namespace npdoc
