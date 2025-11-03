#include <gtest/gtest.h>
#include <vector>

#include "../../src/parse_for_lsp.hpp"
#include "../../src/ast.hpp"

namespace npidltest {

TEST(NPIDL, TestNamespaceSubstitution) {
  auto create_namespace = [](std::string_view full_name) {
    Namespace* cur = nullptr;
    size_t start = 0;
    if (full_name.length() > 2 && full_name.substr(0, 2) == "::") {
      full_name = full_name.substr(2, std::string_view::npos);
      cur = new Namespace(nullptr, "");
    }
    while (start < full_name.size()) {
      size_t dot = full_name.find("::", start);
      if (dot == std::string_view::npos) {
        dot = full_name.size();
      }
      std::string_view part = full_name.substr(start, dot - start);
      if (cur == nullptr) {
        cur = new Namespace(nullptr, std::string(part));
      } else {
        cur = cur->push(std::string(part));
      }
      start = dot + 2;
    }
    return cur;
  };

  auto ns1 = create_namespace("A::B::C::D");
  auto ns2 = create_namespace("A::B::X::Y");
  auto sub = Namespace::substract(ns1, ns2);
  ASSERT_NE(sub.first, nullptr);
  EXPECT_EQ(sub.first->name(), "X");
  EXPECT_EQ(sub.second, 2);

  auto ns3 = create_namespace("A::B::C::D::E::F");
  auto ns4 = create_namespace("A::B::C");
  auto sub2 = Namespace::substract(ns3, ns4);
  ASSERT_EQ(sub2.first, nullptr);
  EXPECT_EQ(sub2.second, 3);

  auto ns5 = create_namespace("X::Y::Z");
  auto ns6 = create_namespace("X::Y::Z");
  auto sub3 = Namespace::substract(ns5, ns6);
  EXPECT_EQ(sub3.first, nullptr);
  EXPECT_EQ(sub3.second, 3);

  auto ns7 = create_namespace("M::N::O");
  auto ns8 = create_namespace("M::N::O::P");
  auto sub4 = Namespace::substract(ns7, ns8);
  ASSERT_NE(sub4.first, nullptr);
  EXPECT_EQ(sub4.first->name(), "P");
  EXPECT_EQ(sub4.second, 3);


  auto ns9 = create_namespace("Q::R::S::T");
  auto ns10 = create_namespace("M::N::Q");
  auto sub5 = Namespace::substract(ns9, ns10);
  ASSERT_NE(sub5.first, nullptr);
  EXPECT_EQ(sub5.first->name(), "M");
  EXPECT_EQ(sub5.second, 0);

  auto ns11 = create_namespace("nprpc");
  auto ns12 = create_namespace("nprpc::detail");

  std::cout << ns11->to_cpp17_namespace() << " vs " << ns12->to_cpp17_namespace() << std::endl;
  std::cout << "Lengths: " << ns11->length() << " vs " << ns12->length() << std::endl;

  auto sub6 = Namespace::substract(ns11, ns12);
  ASSERT_NE(sub6.first, nullptr);
  EXPECT_EQ(sub6.first->name(), "detail");
  EXPECT_EQ(sub6.second, 1);
  EXPECT_EQ(sub6.first->to_cpp17_namespace(sub6.second), "detail");
}


// TEST(NPIDL, TestErrorRecovery) {
//   EXPECT_TRUE(true);
// }

TEST(ErrorRecovery, MultipleErrors) {
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
    npidl::parse_for_lsp(code, errors);

    std::cerr << "Detected " << errors.size() << " errors during parsing.\n";
    for (const auto& err : errors) {
        std::cerr << "Error at line " << err.line << ", col " << err.col << ": " << err.message << "\n";
    }
    
    // Parser detects errors at multiple locations
    // The exact number depends on recovery points
    ASSERT_GE(errors.size(), 2) << "Should detect at least 2 errors";
    
    // Verify we found the key errors (flexible line numbers due to recovery)
    bool found_function_error = false;
    bool found_struct_error = false;
    
    for (const auto& err : errors) {
        if (err.line >= 3 && err.line <= 5) found_function_error = true;
        if (err.line >= 7 && err.line <= 9) found_struct_error = true;
    }
    
    EXPECT_TRUE(found_function_error) << "Should detect error in function declaration";
    EXPECT_TRUE(found_struct_error) << "Should detect error in struct declaration";
}

} // nemespace npidltest

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}