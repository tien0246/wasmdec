# C4 — Differential Runtime qua Biên dịch lại

Đối chiếu ngữ nghĩa pseudocode (từ golden oracle) với module wasm gốc bằng
cách chạy cả hai với cùng inputs và so outputs bit-exact. Phương pháp của
Liu & Wang (ISSTA 2020) và D-Helix (USENIX Security 2024).

## Flow

```
fixture.wasm ──┬── [wasm_runner] wasmtime C API: chạy export với N inputs
               │   → "IN <args> OUT <type>:<hex-bits>" từng dòng
               │
golden/<fn>.c ─┼── [transform.py] strip __usercall/@-annotations/HIDWORD/
               │   LODWORD + prepend runtime_stubs.h + main() driver
               │   → clang -o native_bin
               │
               └── [native_bin] chạy cùng inputs → cùng format output

               [compare.py] so từng dòng → PASS/MISMATCH + exit code
```

## Cách chạy 1 lệnh

```bash
bash tests/diffrt/run_differential.sh <build-dir> [n-inputs]
# VD: bash tests/diffrt/run_differential.sh /private/tmp/wasmhex-v3 64
```

## V1 scope: 6 hàm clean-scalar o2 — 6/6 PASS

| Hàm | Params | Kết quả |
|---|---|---|
| call_two_levels | i32 | PASS (N=128, 0 mismatch) |
| factorial | i32 | PASS (N=128, 0 mismatch) |
| float_ops | f64 | PASS (N=128, 0 mismatch — NaN/±0/±inf edge cases) |
| loop_bound | i32 | PASS (N=128, 0 mismatch) |
| nested_if | i32,i32,i32 | PASS (N=128, 0 mismatch) |
| switch_dispatch | i32,i32 | PASS (N=128, 0 mismatch) |

## Files

| File | Vai trò |
|---|---|
| `runtime_stubs.h` | Định nghĩa mọi scalar helper (wasm_i32_*, wasm_f64_*, fminimum/fmaximum, select, COERCE_*, HIDWORD/LODWORD bit-safe, _DWORD/_QWORD typedefs, wasmhex_global_0 buffer, trap exit(42)) |
| `wasm_runner.c` | Batch wasmtime C API runner — nhận wasm + export + inputs → in kết quả từng dòng |
| `transform.py` | Golden .c → compilable .cc: strip Hex-Rays artifacts + sinh main() driver |
| `gen_inputs.py` | Type-aware input generator: N random + boundary per type (0, ±1, MIN/MAX, NaN, inf, half-even edges) |
| `compare.py` | So 2 output files: bit-exact cho int, NaN-canonicalize cho float, trap match |
| `run_differential.sh` | Driver chính: inputs → wasm_runner → transform+clang → native → compare |

## V2 mở rộng (theo thiết kế C4)

| Phạm vi | Cần thêm | Rủi ro |
|---|---|---|
| o0 (shadow-stack) | wasmhex_global_0 → linear memory buffer mmap; hoặc alloca stack | Raw-address dereference — cần mmap fixed-address hoặc reinterpret as offset |
| pointer args (struct_fields, array_sum, pointer_arith, string_len) | Linear memory model (đọc/ghi từ buffer shared với wasm_runner) | Cần đồng bộ memory giữa 2 runners |
| Tuple returns (wasmhex_tuple_*) | sret struct ABI | Đa dạng tuple types |
| EH (raise_*/throw) | setjmp/longjmp wrapper | Kiểm soát trap matching |
| Memory ops (wasmhex_trap_memory_*) | Bounds-checked buffer access | |

## Ghi chú B4

Các file `.py` trong `tests/diffrt/` là **test tooling** (như fixtures gen),
không phải pipeline decompiler — được giữ lại sau B4. `wasm_runner.c` là
C thuần (không phụ thuộc python). `transform.py` dùng python cho regex
processing thuận tiện; có thể port sang C++ sau nếu cần.
