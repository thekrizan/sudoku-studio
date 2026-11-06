#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ ! -x "$ROOT_DIR/build/sudoku-studio" ] || [ ! -f "$ROOT_DIR/dist/index.html" ]; then
  "$ROOT_DIR/scripts/build.sh"
fi
exec "$ROOT_DIR/build/sudoku-studio" 8080 "$ROOT_DIR/dist"
