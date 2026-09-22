// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "cpp_extract.hpp"
#include "doc_text.hpp"
#include "idl_extract.hpp"
#include "markdown.hpp"
#include "swift_extract.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace npdoc;

namespace {

// A scratch directory removed at the end of the test.
struct TempDir {
  fs::path path;
  TempDir()
      : path(fs::temp_directory_path() /
             ("npdoc_test_" + std::to_string(::getpid()) + "_" +
              ::testing::UnitTest::GetInstance()->current_test_info()->name()))
  {
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  fs::path write(const std::string& name, const std::string& text) const
  {
    const auto p = path / name;
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
    return p;
  }
};

const Symbol* find(const std::vector<Symbol>& symbols, std::string_view id)
{
  auto it = std::find_if(symbols.begin(), symbols.end(),
                         [&](const Symbol& s) { return s.id == id; });
  return it == symbols.end() ? nullptr : &*it;
}

} // namespace

// --- comment text ---------------------------------------------------------

TEST(DocText, StripsLineMarkers)
{
  EXPECT_EQ(strip_comment_markers("/// First line.\n  ///\n  ///   indented"),
            "First line.\n\n  indented");
  EXPECT_EQ(strip_comment_markers("//! Qt style"), "Qt style");
}

TEST(DocText, StripsBlockMarkers)
{
  EXPECT_EQ(strip_comment_markers("/**\n * Summary.\n *\n * Body.\n */"),
            "Summary.\n\nBody.");
  EXPECT_EQ(strip_comment_markers("/** One line. */"), "One line.");
}

TEST(DocText, DoxygenParamsAndReturn)
{
  const auto parts = parse_doxygen(
      "Route dispatch through @p ex (post+wait).\n"
      "@param[in] ex The executor.\n"
      "  Continues here.\n"
      "@param unknown Not a real parameter.\n"
      "@return The builder, for chaining.\n"
      "\n"
      "@note Empty means inline.");
  EXPECT_EQ(parts.doc,
            "Route dispatch through `ex` (post+wait).\n\n"
            "**Note:** Empty means inline.");
  ASSERT_EQ(parts.params.size(), 2u);
  EXPECT_EQ(parts.params[0].name, "ex");
  EXPECT_EQ(parts.params[0].doc, "The executor.\nContinues here.");
  EXPECT_EQ(parts.returns, "The builder, for chaining.");
}

TEST(DocText, DoxygenLeavesCodeFencesAlone)
{
  const auto parts = parse_doxygen("@brief Short.\n\n```cpp\n@param x\n```");
  EXPECT_EQ(parts.doc, "Short.\n\n```cpp\n@param x\n```");
  EXPECT_TRUE(parts.params.empty());
}

TEST(DocText, SwiftCallouts)
{
  const auto parts = parse_swift(
      "Render pages in this process.\n"
      "\n"
      "- Parameters:\n"
      "  - path: The request path.\n"
      "    Second line.\n"
      "  - body: The request body.\n"
      "- Parameter extra: Single form.\n"
      "- Returns: A response, or `nil`.\n"
      "- Throws: Never.");
  EXPECT_EQ(parts.doc, "Render pages in this process.\n\n- Throws: Never.");
  ASSERT_EQ(parts.params.size(), 3u);
  EXPECT_EQ(parts.params[0].name, "path");
  EXPECT_EQ(parts.params[0].doc, "The request path.\nSecond line.");
  EXPECT_EQ(parts.params[1].name, "body");
  EXPECT_EQ(parts.params[2].name, "extra");
  EXPECT_EQ(parts.returns, "A response, or `nil`.");
}

TEST(DocText, Summary)
{
  EXPECT_EQ(summary_of("Two lines\nof summary.\n\nBody."), "Two lines of summary.");
  EXPECT_EQ(summary_of("```\ncode\n```"), "");
}

TEST(Markdown, DocCommentsKeepAngleBrackets)
{
  // In prose, `vector<Post>` must stay text, not become an HTML tag.
  EXPECT_EQ(render_markdown("Returns vector<Post>.", false),
            "<p>Returns vector&lt;Post&gt;.</p>\n");
  EXPECT_EQ(render_inline("Uses `Poa` *now*."),
            "Uses <code>Poa</code> <em>now</em>.");
}

// --- C++ ------------------------------------------------------------------

TEST(CppExtract, PublicApiWithDocs)
{
  TempDir dir;
  dir.write("include/lib/api.hpp", R"(
#pragma once
namespace lib {

/// A widget.
class Widget {
public:
  /// Resize it.
  /// @param w New width.
  /// @param h New height.
  /// @return Whether it changed.
  bool resize(int w, int h);
  bool resize(double scale);
  /// Current width.
  int width() const;
  Widget(const Widget&) = delete;
  /// Public field.
  int tag = 0;
private:
  int secret_;
};

inline bool Widget::resize(double) { return false; }

/// Colors.
enum class Color { Red, /// Green!
  Green };

namespace detail { struct Hidden {}; }

/// Free function.
template <typename T> T identity(T value);

using Size = int;

} // namespace lib
)");
  dir.write("include/lib/impl/internal.hpp", "namespace lib { struct Internal {}; }\n");

  CppOptions opt;
  opt.header_roots = {dir.path / "include/lib"};
  opt.exclude_prefixes = {"impl/"};
  opt.clang_args = {"-std=c++20"};
  opt.root = dir.path;
  const auto symbols = extract_cpp(opt);

  const auto* widget = find(symbols, "cpp:lib::Widget");
  ASSERT_NE(widget, nullptr);
  EXPECT_EQ(widget->kind, "class");
  EXPECT_EQ(widget->doc, "A widget.");
  EXPECT_EQ(widget->signature, "class Widget");
  EXPECT_EQ(widget->file, "include/lib/api.hpp");

  // Overloads get distinct ids; the documented one keeps its params.
  const auto* resize = find(symbols, "cpp:lib::Widget::resize(int, int)");
  ASSERT_NE(resize, nullptr);
  EXPECT_EQ(resize->kind, "method");
  EXPECT_EQ(resize->parent, "cpp:lib::Widget");
  EXPECT_EQ(resize->signature, "bool resize(int w, int h)");
  EXPECT_EQ(resize->doc, "Resize it.");
  EXPECT_EQ(resize->returns, "Whether it changed.");
  ASSERT_TRUE(resize->params);
  ASSERT_EQ(resize->params->size(), 2u);
  EXPECT_EQ((*resize->params)[0].name, "w");
  EXPECT_EQ((*resize->params)[0].type, "int");
  EXPECT_EQ((*resize->params)[0].doc, "New width.");
  EXPECT_NE(find(symbols, "cpp:lib::Widget::resize(double)"), nullptr);

  // Const methods are marked in the id.
  EXPECT_NE(find(symbols, "cpp:lib::Widget::width() const"), nullptr);

  EXPECT_NE(find(symbols, "cpp:lib::Widget::tag"), nullptr);
  EXPECT_EQ(find(symbols, "cpp:lib::Widget::secret_"), nullptr);
  EXPECT_EQ(find(symbols, "cpp:lib::Widget::Widget(const Widget &)"), nullptr);
  EXPECT_EQ(find(symbols, "cpp:lib::detail::Hidden"), nullptr);
  EXPECT_EQ(find(symbols, "cpp:lib::Internal"), nullptr);

  const auto* green = find(symbols, "cpp:lib::Color::Green");
  ASSERT_NE(green, nullptr);
  EXPECT_EQ(green->kind, "case");
  EXPECT_EQ(green->parent, "cpp:lib::Color");
  EXPECT_EQ(green->doc, "Green!");

  const auto* identity = find(symbols, "cpp:lib::identity(T)");
  ASSERT_NE(identity, nullptr);
  EXPECT_EQ(identity->kind, "function");
  ASSERT_TRUE(identity->params);
  EXPECT_EQ((*identity->params)[0].name, "value");

  const auto* size = find(symbols, "cpp:lib::Size");
  ASSERT_NE(size, nullptr);
  EXPECT_EQ(size->kind, "typealias");
  EXPECT_EQ(size->signature, "using Size = int");
}

// --- Swift ----------------------------------------------------------------

TEST(SwiftExtract, SymbolGraph)
{
  TempDir dir;
  const auto graph = dir.write("NPRPC.symbols.json", R"json({
  "metadata": {"formatVersion": {"major": 0}},
  "module": {"name": "NPRPC", "platform": {}},
  "symbols": [
    {"kind": {"identifier": "swift.class", "displayName": "Class"},
     "identifier": {"precise": "s:C1", "interfaceLanguage": "swift"},
     "pathComponents": ["RpcBuilder"],
     "names": {"title": "RpcBuilder"},
     "docComment": {"lines": [{"text": "Builds an Rpc."}]},
     "declarationFragments": [{"kind": "keyword", "spelling": "class"},
                              {"kind": "text", "spelling": " "},
                              {"kind": "identifier", "spelling": "RpcBuilder"}],
     "accessLevel": "public",
     "location": {"uri": "file://)json" + (dir.path / "Sources/RpcBuilder.swift").string() + R"json(",
                  "position": {"line": 9, "character": 0}}},
    {"kind": {"identifier": "swift.method", "displayName": "Instance Method"},
     "identifier": {"precise": "s:M1", "interfaceLanguage": "swift"},
     "pathComponents": ["RpcBuilder", "withHttp(_:)"],
     "names": {"title": "withHttp(_:)"},
     "docComment": {"lines": [{"text": "Enables HTTP."},
                              {"text": "- Parameter port: TCP port."},
                              {"text": "- Returns: Self."}]},
     "declarationFragments": [{"kind": "keyword", "spelling": "func"},
                              {"kind": "text", "spelling": " withHttp(_ port: UInt16) -> Self"}],
     "accessLevel": "public",
     "location": {"uri": "file://)json" + (dir.path / "Sources/RpcBuilder.swift").string() + R"json(",
                  "position": {"line": 20, "character": 2}}},
    {"kind": {"identifier": "swift.var", "displayName": "Global Variable"},
     "identifier": {"precise": "c:@macro@INT16_MAX", "interfaceLanguage": "swift"},
     "pathComponents": ["INT16_MAX"],
     "names": {"title": "INT16_MAX"},
     "declarationFragments": [],
     "accessLevel": "public"},
    {"kind": {"identifier": "swift.func", "displayName": "Function"},
     "identifier": {"precise": "s:G1", "interfaceLanguage": "swift"},
     "pathComponents": ["marshal_Foo(_:)"],
     "names": {"title": "marshal_Foo(_:)"},
     "declarationFragments": [],
     "accessLevel": "public",
     "location": {"uri": "file://)json" + (dir.path / "Sources/Generated/base.swift").string() + R"json(",
                  "position": {"line": 1, "character": 0}}},
    {"kind": {"identifier": "swift.func.op", "displayName": "Operator"},
     "identifier": {"precise": "s:SQsE2neoiySbx_xtFZ::SYNTHESIZED::s:C1", "interfaceLanguage": "swift"},
     "pathComponents": ["RpcBuilder", "!=(_:_:)"],
     "names": {"title": "!=(_:_:)"},
     "declarationFragments": [],
     "accessLevel": "public"}
  ],
  "relationships": [
    {"kind": "memberOf", "source": "s:M1", "target": "s:C1"},
    {"kind": "memberOf", "source": "s:SQsE2neoiySbx_xtFZ::SYNTHESIZED::s:C1", "target": "s:C1"}
  ]
})json");

  const auto symbols = extract_swift(graph, dir.path, {"Sources/Generated/"});
  // Dropped: the synthesized operator, the C macro with no location, and the
  // excluded generated file.
  ASSERT_EQ(symbols.size(), 2u);

  const auto* cls = find(symbols, "swift:NPRPC.RpcBuilder");
  ASSERT_NE(cls, nullptr);
  EXPECT_EQ(cls->kind, "class");
  EXPECT_EQ(cls->signature, "class RpcBuilder");
  EXPECT_EQ(cls->file, "Sources/RpcBuilder.swift");
  EXPECT_EQ(cls->line, 10);

  const auto* method = find(symbols, "swift:NPRPC.RpcBuilder.withHttp(_:)");
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(method->name, "withHttp");
  EXPECT_EQ(method->qualified, "NPRPC.RpcBuilder.withHttp");
  EXPECT_EQ(method->parent, "swift:NPRPC.RpcBuilder");
  EXPECT_EQ(method->doc, "Enables HTTP.");
  EXPECT_EQ(method->returns, "Self.");
  ASSERT_TRUE(method->params);
  EXPECT_EQ((*method->params)[0].name, "port");
}

// --- IDL ------------------------------------------------------------------

TEST(IdlExtract, DocJson)
{
  TempDir dir;
  const auto idl = (dir.path / "idl/blog.npidl").string();
  const auto json = dir.write("blog.doc.json", R"({"format":1,"file":")" + idl +
                                                   R"(","module":"blog","declarations":[
{"kind":"message","name":"Post","namespace":"blog","file":")" + idl + R"(","line":3,"doc":"A post.",
 "fields":[{"name":"cover","type":"string?","line":5,"doc":"Cover URL."}]},
{"kind":"enum","name":"State","namespace":"blog","file":")" + idl + R"(","line":8,"doc":"",
 "underlying":"u8","items":[{"name":"Draft","value":0,"doc":"Hidden."}]},
{"kind":"interface","name":"Blog","namespace":"blog","file":")" + idl + R"(","line":12,"doc":"Access.",
 "trusted":true,"bases":[],"methods":[{"name":"Get","line":14,"doc":"Fetch.","returns":"void",
 "stream":"","unreliable":false,
 "params":[{"name":"slug","direction":"in","direct":false,"type":"string","doc":"Which."},
           {"name":"post","direction":"out","direct":true,"type":"Post","doc":""}],
 "raises":["NotFound"]}]},
{"kind":"const","name":"Max","namespace":"blog","file":")" + idl + R"(","value":"10"}
]})");

  const auto symbols = extract_idl(json, dir.path);

  const auto* post = find(symbols, "idl:blog.Post");
  ASSERT_NE(post, nullptr);
  EXPECT_EQ(post->signature, "message Post");
  EXPECT_EQ(post->file, "idl/blog.npidl");

  const auto* cover = find(symbols, "idl:blog.Post.cover");
  ASSERT_NE(cover, nullptr);
  EXPECT_EQ(cover->signature, "cover?: string");
  EXPECT_EQ(cover->parent, "idl:blog.Post");

  const auto* draft = find(symbols, "idl:blog.State.Draft");
  ASSERT_NE(draft, nullptr);
  EXPECT_EQ(draft->signature, "Draft = 0");

  const auto* blog = find(symbols, "idl:blog.Blog");
  ASSERT_NE(blog, nullptr);
  EXPECT_EQ(blog->signature, "[trusted] interface Blog");

  const auto* get = find(symbols, "idl:blog.Blog.Get");
  ASSERT_NE(get, nullptr);
  EXPECT_EQ(get->signature,
            "void Get(slug: string, post: out direct Post) raises(NotFound)");
  ASSERT_TRUE(get->params);
  EXPECT_EQ((*get->params)[0].direction, "in");
  EXPECT_EQ((*get->params)[0].doc, "Which.");

  const auto* max = find(symbols, "idl:blog.Max");
  ASSERT_NE(max, nullptr);
  EXPECT_EQ(max->kind, "constant");
  EXPECT_EQ(max->signature, "const Max = 10");
}
