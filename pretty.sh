#!/bin/bash

find ./aspire -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cxx" -o -name "*.hxx" -o -name "*.c" -o -name "*.cc" -o -name "*.hh" \) -print0 | xargs -0 clang-format -i

echo "All supported files in $(pwd) formatted with clang-format."