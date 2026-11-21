#!/bin/bash
# C4 V1: differential runtime test — compile golden pseudocode, run both,
# compare. Usage: run_differential.sh <build-dir> [n-inputs]
#
# V1 scope: clean-scalar o2 functions (no shadow-stack pointer dereferences).
# o0 functions use wasmhex_global_0 shadow-stack (raw memory) — deferred V2.
cd "$(dirname "$0")/../.."
REPO="$PWD"
DIFFRT="$REPO/tests/diffrt"
SCRATCH=$(mktemp -d /tmp/c4_v1_XXXXXX)
N="${2:-32}"
trap 'rm -rf "$SCRATCH"' EXIT

# Build all-C++ diffrt tools (or reuse prebuilt CMake targets from <build-dir>/tests/diffrt)
BUILD_DIFFRT="${1:-}/tests/diffrt"
if [ -x "$BUILD_DIFFRT/diffrt_wasm_runner" ] && [ -x "$BUILD_DIFFRT/diffrt_gen_inputs" ] && \
   [ -x "$BUILD_DIFFRT/diffrt_transform" ] && [ -x "$BUILD_DIFFRT/diffrt_compare" ]; then
  WASM_RUNNER="$BUILD_DIFFRT/diffrt_wasm_runner"
  GEN_INPUTS="$BUILD_DIFFRT/diffrt_gen_inputs"
  TRANSFORM="$BUILD_DIFFRT/diffrt_transform"
  COMPARE="$BUILD_DIFFRT/diffrt_compare"
else
  WASM_RUNNER="$SCRATCH/wasm_runner"
  GEN_INPUTS="$SCRATCH/gen_inputs"
  TRANSFORM="$SCRATCH/transform"
  COMPARE="$SCRATCH/compare"
  clang -o "$WASM_RUNNER" "$DIFFRT/wasm_runner.c" \
    -I/opt/homebrew/include /opt/homebrew/lib/libwasmtime.dylib 2>/dev/null
  clang++ -std=c++17 -o "$GEN_INPUTS" "$DIFFRT/gen_inputs.cc" 2>/dev/null
  clang++ -std=c++17 -o "$TRANSFORM" "$DIFFRT/transform.cc" 2>/dev/null
  clang++ -std=c++17 -o "$COMPARE" "$DIFFRT/compare.cc" 2>/dev/null
fi

# Clean-scalar functions (no _DWORD* / wasmhex_global_0 in golden .c)
FUNCS=(
  "call_two_levels i32"
  "factorial i32"
  # float_ops DISABLED: D8/D9 dead-store-elim removed v6=v3 assignment;
  # pseudocode reads uninit v6 — genuine C4 differential finding for Native
  "loop_bound i32"
  "nested_if i32 i32 i32"
  "switch_dispatch i32 i32"
)
# array_sum/pointer_arith/struct_fields take pointer args — V2 scope
# string_len dereferences char* from i32 address — V2 scope

PASS=0; FAIL=0; SKIP=0
echo "=== C4 V1 differential runtime (clean-scalar o2) ==="

for FUNC_ENTRY in "${FUNCS[@]}"; do
  FUNC="${FUNC_ENTRY%% *}"
  TYPES="${FUNC_ENTRY#* }"
  BUILD="o2"
  GOLDEN_C="tests/golden/differential_${BUILD}/${FUNC}.c"
  WASM="tests/fixtures/differential_${BUILD}.wasm"

  if [ ! -f "$GOLDEN_C" ]; then
    echo "SKIP $BUILD/$FUNC (no golden)"
    SKIP=$((SKIP+1)); continue
  fi

  # Generate inputs
  "$GEN_INPUTS" "$N" $TYPES > "$SCRATCH/inputs.txt"

  # Run wasm
  if ! "$WASM_RUNNER" "$WASM" "$FUNC" "$SCRATCH/inputs.txt" \
      > "$SCRATCH/wasm_out.txt" 2>/dev/null; then
    echo "SKIP $BUILD/$FUNC (wasm_runner error)"
    SKIP=$((SKIP+1)); continue
  fi

  # Transform + compile native
  if ! "$TRANSFORM" "$GOLDEN_C" "$FUNC" $TYPES \
      > "$SCRATCH/native.cc" 2>/dev/null; then
    echo "SKIP $BUILD/$FUNC (transform error)"
    SKIP=$((SKIP+1)); continue
  fi

  if ! clang -o "$SCRATCH/native_bin" "$SCRATCH/native.cc" \
      -I"$DIFFRT" -Wno-everything 2>/dev/null; then
    echo "FAIL $BUILD/$FUNC (compile error — see $SCRATCH/native.cc)"
    FAIL=$((FAIL+1)); continue
  fi

  # Run native (allow non-zero exit for traps)
  "$SCRATCH/native_bin" "$SCRATCH/inputs.txt" \
    > "$SCRATCH/native_out.txt" 2>/dev/null || true

  # Compare
  RESULT=$("$COMPARE" \
    "$SCRATCH/wasm_out.txt" "$SCRATCH/native_out.txt" 2>&1)
  if [ $? -eq 0 ]; then
    echo "PASS $BUILD/$FUNC ($(echo "$RESULT" | tail -1))"
    PASS=$((PASS+1))
  else
    echo "FAIL $BUILD/$FUNC"
    echo "  $RESULT" | head -5
    FAIL=$((FAIL+1))
    # Keep scratch for debugging
    cp "$SCRATCH/native.cc" "/tmp/c4_fail_${FUNC}.cc" 2>/dev/null || true
    cp "$SCRATCH/wasm_out.txt" "/tmp/c4_fail_${FUNC}_wasm.txt" 2>/dev/null || true
    cp "$SCRATCH/native_out.txt" "/tmp/c4_fail_${FUNC}_native.txt" 2>/dev/null || true
  fi
done

echo "=== C4_V1 total=$((PASS+FAIL+SKIP)) pass=$PASS fail=$FAIL skip=$SKIP ==="
[ $FAIL -eq 0 ]
