// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// npdoc: merges the public API of the C++ headers, the Swift package and the
// IDL files, with their doc comments, into one JSON model for the docs site.

#include "cpp_extract.hpp"
#include "idl_extract.hpp"
#include "markdown.hpp"
#include "model.hpp"
#include "swift_extract.hpp"

#include <boost/program_options.hpp>
#include <glaze/glaze.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
namespace po = boost::program_options;

namespace {

void print_stats(const std::vector<npdoc::Symbol>& symbols)
{
  std::map<std::string, std::pair<size_t, size_t>> by_lang; // total, documented
  for (const auto& s : symbols) {
    auto& [total, documented] = by_lang[s.lang];
    ++total;
    documented += !s.doc.empty();
  }
  for (const auto& [lang, counts] : by_lang)
    std::cerr << "npdoc: " << lang << ": " << counts.first << " symbols, "
              << counts.second << " documented\n";
}

void render_html(std::vector<npdoc::Symbol>& symbols)
{
  for (auto& s : symbols) {
    s.doc_html = npdoc::render_markdown(s.doc, false);
    s.summary_html = npdoc::render_inline(s.summary);
    if (s.returns)
      s.returns_html = npdoc::render_inline(*s.returns);
    if (s.params)
      for (auto& p : *s.params)
        p.doc_html = npdoc::render_inline(p.doc);
  }
}

std::vector<npdoc::Guide> read_guides(const std::vector<fs::path>& dirs,
                                      const fs::path& root)
{
  std::vector<npdoc::Guide> guides;
  for (const auto& dir : dirs) {
    for (const auto& entry : fs::directory_iterator(dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".md")
        continue;
      std::ifstream in(entry.path(), std::ios::binary);
      std::stringstream text;
      text << in.rdbuf();
      const auto md = text.str();

      npdoc::Guide g;
      g.slug = entry.path().stem().string();
      g.title = g.slug;
      std::istringstream lines(md);
      for (std::string line; std::getline(lines, line);) {
        if (line.starts_with("# ")) {
          g.title = line.substr(2);
          break;
        }
      }
      g.file = fs::weakly_canonical(entry.path())
                   .lexically_relative(fs::weakly_canonical(root))
                   .generic_string();
      // Guides are ours and may use HTML (tables, <details>) on purpose.
      g.html = npdoc::render_markdown(md, true);
      guides.push_back(std::move(g));
    }
  }
  std::sort(guides.begin(), guides.end(),
            [](const auto& a, const auto& b) { return a.slug < b.slug; });
  return guides;
}

} // namespace

int main(int argc, char* argv[])
{
  fs::path root = fs::current_path();
  fs::path out_path;
  std::vector<fs::path> cpp_headers, swift_graphs, idl_files, guide_dirs;
  std::vector<std::string> cpp_excludes, swift_excludes, clang_args;
  bool list_undocumented = false;

  po::options_description desc(
      "npdoc: collect API docs from C++, Swift and IDL into one JSON file");
  desc.add_options()
    ("help", "show this help")
    ("root", po::value(&root),
     "base directory for the `file` of every symbol (default: current dir)")
    ("cpp-headers", po::value(&cpp_headers)->composing(),
     "directory of public C++ headers; repeatable")
    ("cpp-exclude", po::value(&cpp_excludes)->composing(),
     "skip headers whose path under --cpp-headers starts with this, e.g. impl/")
    ("clang-arg", po::value(&clang_args)->composing(),
     "argument for libclang, e.g. --clang-arg=-Iinclude; repeatable")
    ("swift-symbols", po::value(&swift_graphs)->composing(),
     "<Module>.symbols.json from swiftc -emit-symbol-graph; repeatable")
    ("swift-exclude", po::value(&swift_excludes)->composing(),
     "skip Swift symbols in files whose path under --root starts with this")
    ("idl", po::value(&idl_files)->composing(),
     "<file>.doc.json from npidl --doc-json; repeatable")
    ("guides", po::value(&guide_dirs)->composing(),
     "directory of Markdown guides (*.md, not recursive); repeatable")
    ("out", po::value(&out_path), "output file (default: stdout)")
    ("list-undocumented", po::bool_switch(&list_undocumented),
     "print the id of every symbol without a doc to stderr");

  try {
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
      std::cout << desc << '\n';
      return 0;
    }
  } catch (const po::error& e) {
    std::cerr << "npdoc: " << e.what() << '\n' << desc << '\n';
    return 2;
  }

  try {
    npdoc::Api api;
    auto append = [&](std::vector<npdoc::Symbol>&& symbols) {
      std::move(symbols.begin(), symbols.end(), std::back_inserter(api.symbols));
    };

    if (!cpp_headers.empty())
      append(npdoc::extract_cpp({cpp_headers, cpp_excludes, clang_args, root}));
    for (const auto& g : swift_graphs)
      append(npdoc::extract_swift(g, root, swift_excludes));
    for (const auto& f : idl_files)
      append(npdoc::extract_idl(f, root));

    // Stable output: rerunning on unchanged sources gives an identical file,
    // so it diffs cleanly.
    std::sort(api.symbols.begin(), api.symbols.end(),
              [](const npdoc::Symbol& a, const npdoc::Symbol& b) {
                return a.id < b.id;
              });

    render_html(api.symbols);
    api.guides = read_guides(guide_dirs, root);

    print_stats(api.symbols);
    if (!api.guides.empty())
      std::cerr << "npdoc: guides: " << api.guides.size() << '\n';
    if (list_undocumented)
      for (const auto& s : api.symbols)
        if (s.doc.empty())
          std::cerr << "undocumented: " << s.id << '\n';

    std::string json;
    if (auto ec = glz::write<glz::opts{.prettify = true}>(api, json))
      throw std::runtime_error("failed to serialize the API model");
    json += '\n';

    if (out_path.empty()) {
      std::cout << json;
    } else {
      std::ofstream out(out_path, std::ios::binary);
      if (!out)
        throw std::runtime_error("cannot write " + out_path.string());
      out << json;
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "npdoc: error: " << e.what() << '\n';
    return 1;
  }
}
