#!/bin/bash

# Find and format all C++ source files
# Excludes third_party and build directories

echo "Formatting C++ files..."

find . \
    -type d \( -name "third_party" -o -name "build" -o -name ".build*" -o -name ".git" -o -name "node_modules" \) -prune -o \
    -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.c" \) \
    -print0 | xargs -0 clang-format -i -verbose

echo "Done."
