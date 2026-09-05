// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/ast.hpp"
#include "../../src/parse_for_lsp.hpp"
#include "../../src/position_index.hpp"
#include "../../src/position_index_builder.hpp"

namespace npidltest {

using namespace npidl;

TEST(NPIDL, TestNamespaceSubstitution)
{
  auto root_namespace = new Namespace(nullptr, "<root>");
  auto create_namespace = [root_namespace](std::string_view full_name) {
    Namespace* cur = root_namespace;
    size_t start = 0;
    if (full_name.length() > 2 && full_name.substr(0, 2) == "::")
      full_name = full_name.substr(2, std::string_view::npos);

    while (start < full_name.size()) {
      size_t dot = full_name.find("::", start);
      if (dot == std::string_view::npos) {
        dot = full_name.size();
      }
      std::string_view part = full_name.substr(start, dot - start);
      start = dot + 2;

      Namespace* child = cur->find_child(std::string(part));
      if (child == nullptr) cur = cur->push(std::string(part));
      else cur = child;
    }
    return cur;
  };

  auto n1 = create_namespace("A::B::C::D");
  auto n2 = create_namespace("A::B::X::Y");
  int level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 3);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "X::Y");

  n1 = create_namespace("A::B::C::D::E::F");
  n2 = create_namespace("A::B::C");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 4);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "");

  n1 = create_namespace("X::Y::Z");
  n2 = create_namespace("X::Y::Z");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 4);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "");

  n1 = create_namespace("M::N::O");
  n2 = create_namespace("M::N::O::P");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 4);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "P");

  n1 = create_namespace("Q::R::S::T");
  n2 = create_namespace("M::N::Q");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 0);
  // to_cpp17_namespace strips a leading '::' from the global path
  EXPECT_EQ(n2->to_cpp17_namespace(level), "M::N::Q");

  n1 = create_namespace("nprpc");
  n2 = create_namespace("nprpc::detail::helpers");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 2);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "detail::helpers");

  n1 = create_namespace("myinterface::something");
  n2 = create_namespace("nprpc::detail::helpers");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 0);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "nprpc::detail::helpers");

  delete root_namespace;
}

// TEST(NPIDL, TestErrorRecovery) {
//   EXPECT_TRUE(true);
// }

TEST(ErrorRecovery, MultipleErrors)
{
  std::string code = R"(
        namespace Test {
            interface Foo {
                void bad(i32 x, i32 y)  // Error 1: missing semicolon
                void good(i32 z);       // Should parse
            };
            struct Bar {
                i32 a  // Error 2: missing semicolon
                i32 b;
            };
        };
    )";

  std::vector<npidl::ParseError> errors;
  npidl::parse_string_for_testing(code, errors);

  std::cerr << "Detected " << errors.size() << " errors during parsing.\n";
  for (const auto& err : errors) {
    std::cerr << "Error at line " << err.line << ", col " << err.col << ": "
              << err.message << "\n";
  }

  // Parser detects errors at multiple locations
  // The exact number depends on recovery points
  ASSERT_GE(errors.size(), 2) << "Should detect at least 2 errors";

  // Verify we found the key errors (flexible line numbers due to recovery)
  bool found_function_error = false;
  bool found_struct_error = false;

  for (const auto& err : errors) {
    if (err.line >= 3 && err.line <= 5)
      found_function_error = true;
    if (err.line >= 7 && err.line <= 9)
      found_struct_error = true;
  }

  EXPECT_TRUE(found_function_error)
      << "Should detect error in function declaration";
  EXPECT_TRUE(found_struct_error)
      << "Should detect error in struct declaration";
}

TEST(NPIDL, MultipleRaisesSyntax)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;
  std::string code = R"(
    module sample;

    exception FirstError {
      message: string;
    }

    exception SecondError {
      message: string;
      code: u32;
    }

    interface Demo {
      void DoThing() raises (FirstError, SecondError);
    }
  )";

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, code, errors));
  ASSERT_TRUE(errors.empty());
  ASSERT_EQ(ctx.interfaces.size(), 1u);
  ASSERT_EQ(ctx.interfaces[0]->fns.size(), 1u);

  auto* fn = ctx.interfaces[0]->fns[0];
  ASSERT_TRUE(fn->is_throwing());
  ASSERT_EQ(fn->exceptions.size(), 2u);
  EXPECT_EQ(fn->exceptions[0]->name, "FirstError");
  EXPECT_EQ(fn->exceptions[1]->name, "SecondError");
}

TEST(LspReturnTypes, BidiStreamReturnTypeRefs)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;
  std::string code = R"(
module sample;

message ThemeAck { ok: boolean; }
message SystemTheme { name: string; }

interface Theme {
  bidi_stream<ThemeAck, SystemTheme> SubscribeSystemTheme();
}
)";

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, code, errors))
      << (errors.empty() ? "" : errors.front().message);
  ASSERT_EQ(ctx.interfaces.size(), 1u);
  ASSERT_EQ(ctx.interfaces[0]->fns.size(), 1u);

  auto* fn = ctx.interfaces[0]->fns[0];
  ASSERT_EQ(fn->ret_type_refs.size(), 3u);
  EXPECT_TRUE(fn->ret_type_refs[0].is_keyword);
  EXPECT_EQ(fn->ret_type_refs[0].keyword, "bidi_stream");
  ASSERT_NE(fn->ret_type_refs[1].type, nullptr);
  ASSERT_NE(fn->ret_type_refs[2].type, nullptr);
  EXPECT_EQ(npidl::cflat(fn->ret_type_refs[1].type)->name, "ThemeAck");
  EXPECT_EQ(npidl::cflat(fn->ret_type_refs[2].type)->name, "SystemTheme");
}

TEST(LspReturnTypes, EnumParamTypeRefs)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;
  std::string code = R"(
module sample;

enum PanelEdge: u32 {
  top,
  bottom,
  left,
  right
};

interface Demo {
  u32 CreatePanel(arenaId: in string, edge: in PanelEdge,
                  thickness: in u32, reserve: in boolean,
                  title: in string, appId: in string);
}
)";

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, code, errors))
      << (errors.empty() ? "" : errors.front().message);
  auto* fn = ctx.interfaces[0]->fns[0];
  npidl::AstFunctionArgument* edge = nullptr;
  for (auto* a : fn->args) {
    if (a->name == "edge")
      edge = a;
  }
  ASSERT_NE(edge, nullptr);
  ASSERT_EQ(edge->type->id, npidl::FieldType::Enum) << (int)edge->type->id;
  ASSERT_EQ(edge->type_refs.size(), 1u) << edge->type_refs.size();
  EXPECT_EQ(edge->type_refs[0].type->id, npidl::FieldType::Enum);

  npidl::PositionIndex index;
  npidl::PositionIndexBuilder(index, ctx).build();
  const auto& site = edge->type_refs[0];
  for (auto* a : fn->args) {
    if (a->name == "edge")
      continue;
    if (!a->type_ref_range.is_valid())
      continue;
    EXPECT_FALSE(a->type_ref_range.start.line == site.range.start.line &&
                 a->type_ref_range.start.column == site.range.start.column)
        << "arg " << a->name
        << " inherited PanelEdge type_ref_range from a reused argument";
  }
  int hits = 0;
  int enum_hits = 0;
  int alias_hits = 0;
  for (const auto& e : index.entries()) {
    if (e.start_line == site.range.start.line &&
        e.start_col == site.range.start.column) {
      ++hits;
      if (e.node_type == npidl::PositionIndex::NodeType::Enum)
        ++enum_hits;
      if (e.node_type == npidl::PositionIndex::NodeType::Alias)
        ++alias_hits;
    }
  }
  EXPECT_EQ(hits, 1) << "duplicate index entries at enum param type";
  EXPECT_EQ(enum_hits, 1);
  EXPECT_EQ(alias_hits, 0);

  const auto* entry = index.find_at_position(site.range.start.line,
                                             site.range.start.column);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->node_type, npidl::PositionIndex::NodeType::Enum);
}

TEST(LspReturnTypes, RaisesExceptionGoto)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;
  std::string code = R"(
module sample;

exception SurfaceNotFound { id: u32; }

interface Demo {
  void DestroySurface(surfaceId: in u32) raises(SurfaceNotFound);
}
)";

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, code, errors))
      << (errors.empty() ? "" : errors.front().message);
  auto* fn = ctx.interfaces[0]->fns[0];
  bool found = false;
  for (const auto& site : fn->ret_type_refs) {
    if (!site.is_keyword && site.type &&
        npidl::cflat(site.type)->name == "SurfaceNotFound") {
      found = true;
      npidl::PositionIndex index;
      npidl::PositionIndexBuilder(index, ctx).build();
      const auto* entry = index.find_at_position(site.range.start.line,
                                                 site.range.start.column);
      ASSERT_NE(entry, nullptr);
      EXPECT_EQ(entry->node_type, npidl::PositionIndex::NodeType::Exception);
      auto* ex = static_cast<npidl::AstStructDecl*>(entry->node);
      EXPECT_TRUE(ex->name_range.is_valid());
      EXPECT_NE(ex->name_range.start.line, site.range.start.line);
    }
  }
  EXPECT_TRUE(found) << "raises() type ref for SurfaceNotFound missing";
}

static const char* kValidSample = R"(
module sample;

message Point {
  x: i32;
  y: i32;
}

interface Demo {
  void Move(p: Point);
}
)";

static bool has_redefinition(const std::vector<npidl::ParseError>& errors)
{
  for (const auto& err : errors) {
    if (err.message.find("redefinition") != std::string::npos)
      return true;
  }
  return false;
}

TEST(LspReparse, SameContentTwiceHasNoRedefinition)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, kValidSample, errors)) << (errors.empty() ? "" : errors.front().message);
  ASSERT_TRUE(errors.empty());
  ASSERT_EQ(ctx.interfaces.size(), 1u);
  ASSERT_EQ(ctx.interfaces[0]->fns.size(), 1u);
  EXPECT_EQ(ctx.interfaces[0]->fns[0]->name, "Move");

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, kValidSample, errors)) << (errors.empty() ? "" : errors.front().message);
  ASSERT_TRUE(errors.empty());
  EXPECT_FALSE(has_redefinition(errors));
  ASSERT_EQ(ctx.interfaces.size(), 1u);
  ASSERT_EQ(ctx.interfaces[0]->name, "Demo");
}

TEST(LspReparse, EditIntroducesThenClearsError)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, kValidSample, errors));

  std::string invalid = R"(
module sample;

message Point {
  x: UnknownType;
  y: i32;
}

interface Demo {
  void Move(p: Point);
}
)";

  EXPECT_FALSE(npidl::parse_for_lsp(ctx, invalid, errors));
  ASSERT_FALSE(errors.empty());
  EXPECT_FALSE(has_redefinition(errors))
      << "Reparse after edit should not report stale type redefinitions; got: "
      << errors.front().message;

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, kValidSample, errors)) << (errors.empty() ? "" : errors.front().message);
  EXPECT_TRUE(errors.empty());
  ASSERT_EQ(ctx.interfaces.size(), 1u);
}

TEST(LspReparse, AddTypeAndUseItAfterEdit)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;

  std::string before = R"(
module sample;

interface Demo {
  void Ping();
}
)";

  std::string after = R"(
module sample;

message Point {
  x: i32;
  y: i32;
}

interface Demo {
  void Ping();
  void Move(p: Point);
}
)";

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, before, errors));
  ASSERT_EQ(ctx.interfaces.size(), 1u);
  ASSERT_EQ(ctx.interfaces[0]->fns.size(), 1u);

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, after, errors)) << (errors.empty() ? "" : errors.front().message);
  ASSERT_TRUE(errors.empty());
  ASSERT_EQ(ctx.interfaces.size(), 1u);
  ASSERT_EQ(ctx.interfaces[0]->fns.size(), 2u);
  EXPECT_EQ(ctx.interfaces[0]->fns[1]->name, "Move");
  ASSERT_FALSE(ctx.interfaces[0]->fns[1]->args.empty());
  EXPECT_EQ(ctx.interfaces[0]->fns[1]->args[0]->name, "p");
}

TEST(LspReparse, PositionIndexTracksFunctionNameNotWholeSignature)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;
  ASSERT_TRUE(npidl::parse_for_lsp(ctx, kValidSample, errors));

  npidl::PositionIndex index;
  npidl::PositionIndexBuilder builder(index, ctx);
  builder.build();

  // `void Move(p: Point);` lives inside interface Demo. The function token
  // must be just "Move", not the entire signature.
  const npidl::PositionIndex::Entry* fn = nullptr;
  for (const auto& entry : index.entries()) {
    if (entry.node_type == npidl::PositionIndex::NodeType::Function) {
      fn = &entry;
      break;
    }
  }
  ASSERT_NE(fn, nullptr);
  EXPECT_EQ(fn->start_line, fn->end_line);
  EXPECT_EQ(fn->end_col - fn->start_col + 1, 4u); // "Move"

  // Reparse and confirm the index is rebuilt against the new AST, not the old
  // one (stale pointers / duplicate entries).
  ASSERT_TRUE(npidl::parse_for_lsp(ctx, kValidSample, errors));
  index.clear();
  npidl::PositionIndexBuilder builder2(index, ctx);
  builder2.build();

  size_t function_tokens = 0;
  for (const auto& entry : index.entries()) {
    if (entry.node_type == npidl::PositionIndex::NodeType::Function)
      ++function_tokens;
  }
  EXPECT_EQ(function_tokens, 1u);
}

TEST(LspReparse, HoverPositionSurvivesContentEdit)
{
  npidl::Context ctx;
  std::vector<npidl::ParseError> errors;

  // No leading newline so line numbers stay obvious. Adding a field to Point
  // must not disturb lookup of the parameter type on the following line.
  std::string before =
      "module sample;\n"
      "message Point { x: i32; y: i32; }\n"
      "interface Demo { void Move(p: Point); }\n";
  std::string after =
      "module sample;\n"
      "message Point { x: i32; y: i32; z: i32; }\n"
      "interface Demo { void Move(p: Point); }\n";

  auto point_usage = [](const std::string& src) {
    // 1-based line/col of the parameter type "Point"
    const std::string needle = "p: Point";
    auto pos = src.find(needle);
    EXPECT_NE(pos, std::string::npos);
    uint32_t line = 1, col = 1;
    for (size_t i = 0; i < pos; ++i) {
      if (src[i] == '\n') {
        ++line;
        col = 1;
      } else {
        ++col;
      }
    }
    col += 3; // skip "p: "
    return std::pair<uint32_t, uint32_t>{line, col};
  };

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, before, errors));
  npidl::PositionIndex index;
  npidl::PositionIndexBuilder(index, ctx).build();

  auto [line, col] = point_usage(before);
  const auto* before_entry = index.find_at_position(line, col);
  ASSERT_NE(before_entry, nullptr);
  EXPECT_EQ(before_entry->node_type, npidl::PositionIndex::NodeType::Struct);

  ASSERT_TRUE(npidl::parse_for_lsp(ctx, after, errors));
  index.clear();
  npidl::PositionIndexBuilder(index, ctx).build();

  auto [line2, col2] = point_usage(after);
  EXPECT_EQ(line, line2);
  EXPECT_EQ(col, col2);

  const auto* after_entry = index.find_at_position(line2, col2);
  ASSERT_NE(after_entry, nullptr);
  EXPECT_EQ(after_entry->node_type, npidl::PositionIndex::NodeType::Struct);
}

} // namespace npidltest

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}