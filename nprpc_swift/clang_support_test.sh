#!/bin/bash
docker run --rm -i swift:6.3.0 bash -c 'clang -v && cat<<EOF
#include <iostream>
#include <functional>
int main() {
    std::move_only_function<void()> lambda = []() {
        std::cout << "Hello from a move-only lambda!" << std::endl;
    };
    lambda();
    std::cout << "Hello from Clang in Swift Docker container!" << std::endl;
    return 0;
}
EOF' > /tmp/clang_test.cpp && clang++ -std=c++23 -x c++ /tmp/clang_test.cpp -o /tmp/clang_test && /tmp/clang_test