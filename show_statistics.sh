#!/bin/bash

cloc . --exclude-lang=SVG,XML,zsh \
  --fullpath --not-match-d='build|gen|Generated|node_modules|third_party|dist|\.build_.*|\.cache|\.github|\.svelte-kit|\.clang-format' \
  --not-match-f='nprpc_nameserver\.hpp|package-lock\.json|nprpc_node\.hpp|nprpc_base\.hpp'