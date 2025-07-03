#!/bin/bash

find . -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cxx" -o -name "*.hxx" -o -name "*.c" -o -name "*.h" -o -name "*.cc" -o -name "*.hh" \) | while read file; do
    clang-format -i "$file"
    echo "Formatted: $file"
done