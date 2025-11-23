#!/usr/bin/env bash
# Run the canonical LayoutPlayground build (build_main)
# Usage: ./run_layout_playground.sh

set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BINARY="$PROJECT_ROOT/build_main/LayoutPlayground_artefacts/Debug/Layout Playground.app/Contents/MacOS/Layout Playground"
if [ ! -x "$BINARY" ]; then
  echo "Error: binary not found or not executable: $BINARY"
  echo "Try building with: cmake --build build_main --target LayoutPlayground -j 6"
  exit 1
fi
# Open the .app on macOS so it shows as an app in Dock
open "$PROJECT_ROOT/build_main/LayoutPlayground_artefacts/Debug/Layout Playground.app"
# Also run the inner binary in foreground to show stderr if needed
# "$BINARY"
