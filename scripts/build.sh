#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

npm ci
npm run build
clang -std=c11 -O2 -Wall -Wextra -Wpedantic \
  -DNO_SSL -DNO_WEBSOCKET \
  -Ithird_party/civetweb/include \
  main.c third_party/civetweb/src/civetweb.c \
  -o build/sudoku-studio
