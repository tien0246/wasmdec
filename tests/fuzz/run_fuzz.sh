#!/bin/bash
# C3 fuzz runner. Fails (non-zero) when the target cannot be built or when a
# crash/sanitizer report is found; it never reports success without running.
#   bash tests/fuzz/run_fuzz.sh [seconds]           # libFuzzer (clang)
#   bash tests/fuzz/run_fuzz.sh --standalone [iter] # any compiler, ASan+UBSan
set -euo pipefail
cd "$(dirname "$0")/../.."
BUILD=build/fuzz
cmake -S . -B "$BUILD" -DWASMHEX_NATIVE=OFF -DWASMHEX_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null

if [ "${1:-}" = "--standalone" ]; then
  ITERATIONS="${2:-200}"
  cmake --build "$BUILD" --target fuzz_parse_standalone -j"$(getconf _NPROCESSORS_ONLN)"
  "$BUILD/tests/fuzz/fuzz_parse_standalone" "$ITERATIONS" tests/fixtures/*.wasm
  exit $?
fi

DURATION="${1:-30}"
cmake --build "$BUILD" --target fuzz_parse -j"$(getconf _NPROCESSORS_ONLN)"
CORPUS="$BUILD/corpus"
ARTIFACTS="$BUILD/artifacts"
mkdir -p "$CORPUS" "$ARTIFACTS"
cp tests/fixtures/*.wasm "$CORPUS/"
"$BUILD/tests/fuzz/fuzz_parse" -max_total_time="$DURATION" \
  -artifact_prefix="$ARTIFACTS/" "$CORPUS"
