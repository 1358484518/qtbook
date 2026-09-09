#!/usr/bin/env bash
# Build and run the Qt6 smoke test headlessly, proving the toolchain works.
# Usage: bash .cursor/smoke-test/run.sh [output.png]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$HERE/render.png}"

cmake -S "$HERE" -B "$HERE/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$HERE/build" -j"$(nproc)"

QT_QPA_PLATFORM=offscreen "$HERE/build/qt_env_smoke" "$OUT"
echo "Smoke test PNG written to: $OUT"
