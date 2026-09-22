find_package(GTest REQUIRED)

add_executable(npdoc_test main.cpp)
target_link_libraries(npdoc_test PRIVATE npdoc_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(npdoc_test
  PROPERTIES TIMEOUT 120
  DISCOVERY_MODE PRE_TEST
)
