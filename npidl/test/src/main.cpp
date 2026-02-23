// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <vector>

#include "../../src/ast.hpp"
#include "../../src/parse_for_lsp.hpp"

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
  EXPECT_EQ(n2->to_cpp17_namespace(level), "::M::N::Q");

  n1 = create_namespace("nprpc");
  n2 = create_namespace("nprpc::detail::helpers");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 2);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "detail::helpers");

  n1 = create_namespace("myinterface::something");
  n2 = create_namespace("nprpc::detail::helpers");
  level = Namespace::substract(n1, n2);
  EXPECT_EQ(level, 0);
  EXPECT_EQ(n2->to_cpp17_namespace(level), "::nprpc::detail::helpers");
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

} // namespace npidltest

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}