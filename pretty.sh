#!/bin/bash

format_cpp_files() {
  local dir="$1"
  for entry in "$dir"/*; do
    if [ -d "$entry" ]; then

      format_cpp_files "$entry"
    elif [ -f "$entry" ]; then

      case "$entry" in
        *.cpp|*.hpp|*.h|*.cxx|*.hxx|*.c|*.cc|*.hh)
          clang-format -i "$entry"
          ;;
      esac
    fi
  done
}

format_cpp_files "."

echo "All supported files in $(pwd) formatted with clang-format."
