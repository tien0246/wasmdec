# wasmhex Test Suite (All-C++ Toolchain — M6-S4 / P5)

Toàn bộ hệ thống kiểm thử của `wasmhex` chạy hoàn toàn bằng C++ (không còn bất kỳ script `.py`, `.pyc`, `.mjs`, `.js` nào trong repo).

## 1. Bảng ánh xạ lệnh điều phối (Old Python/Node → New C++ Invocation)

| Lệnh cũ (Python / Node) | Lệnh C++ mới (`wasmdec_runner` / `runtime_oracle`) | Ghi chú |
|---|---|---|
| `python3 tests/golden/dump_oracle.py --idat <idat> --snapshot <idausr>` | `<build>/wasmdec_runner dump-golden [<fixture> ...] [--out-root tests/golden] [--idausr <idausr>]` | B1/C1 golden dumper qua `idalib` (thay cả `oracle_ida.py`) |
| `python3 tests/golden/run_expect.py [fixture ...]` | `<build>/wasmdec_runner expect [<fixture> ...]` | Chạy toàn bộ 45 bộ `tests/golden/*/expect.txt` |
| `idat ... -Sverify_wasmhex_duckdb_all_f5.py <module.wasm>` | `<build>/wasmdec_runner f5 <module.wasm> [--stride N] [--shard S] [--limit L] [--bounds hidden\|explicit]` | K6: luôn bật `WASMHEX_DEBUG=verify`, `cfunc->verify`, `mba->verify`, in `F5_SUBSET_SHA256` |
| `python3 tests/integration/ida/check_wasmhex_duckdb_sample.py <idat> <idausr>` | `<build>/wasmdec_runner sample [<module.wasm>] [--stride 100] [--max-failures 1] [--idausr <idausr>]` | Chạy cả 2 mode `hidden` + `explicit`, in `WASMHEX_P74_*` và `F5_SUBSET_SHA256` |
| `python3 tests/integration/ida/check_wasmhex_brtable_matrix.py <idat> <idausr>` | `<build>/wasmdec_runner matrix [--idausr <idausr>]` | 12 `bt_*` fixtures × (`node` qua `libwasmtime` + `jtbl` qua `idalib`) = 24/24 |
| `WASMHEX_GOLDEN_STRICT=1 python3 tests/integration/ida/check_wasmhex_golden_merge.py <idat> <idausr>` | `<build>/wasmdec_runner golden-merge [--strict] [--regenerate] [--idausr <idausr>]` | So khớp 6 metrics + strict line-by-line với `tests/integration/golden/mergeProcessed_baseline.txt` |
| `python3 tests/integration/ida/check_wasmhex_<case>.py <idat> <idausr>` | `<build>/wasmdec_runner check <case> [--idausr <idausr>]` | Chạy từng checker cụ thể (vd: `p0_regressions`, `p1_regressions`, `p4_idioms`, `prototypes`, `names`, `dwarf_names`, `differential`, `cframe_fallback`, ...) |
| `idat ... -Sverify_wasmhex_<case>.py <fixture.wasm>` | `<build>/wasmdec_runner check <case> [--idausr <idausr>]` | Chạy các verifier bespoke (vd: `p04`, `p05`, `p06`, `s9`, `s10`, `s10_corpus_marks`, `s1s2s7s11`, `s7`, `tailcall_xrefs`, `float_unary_raw`, `memory_space`, ...) |
| Chạy toàn bộ bespoke checkers nhanh | `<build>/wasmdec_runner check all [--idausr <idausr>]` | Chạy toàn bộ 44 suite kiểm thử IDA trong một tiến trình `idalib` |
| (mới — ABI audit) | `<build>/wasmdec_runner abi-probe <module.wasm> [--function <name>] [--idausr <idausr>]` | Thu thập bằng chứng ABI: regmap mreg/mreg2reg mọi thanh ghi, verify_argloc reg1/reg2 @8B, cưỡng ép e2e `retloc reg1(rN)` + return `__int64`, thứ tự callback hexrays (LIFO probe), census `__SET_PAIR__`/`__PAIR64__`/`LODWORD`/`HIDWORD` theo tên hàm (`--function`) |
| `node tests/integration/runtime/<script>.mjs` (59 kịch bản) | `<build>/tests/runtime_oracle/runtime_oracle --cases tests/runtime_oracle/cases.tsv --fixtures tests/fixtures` | C++ runtime oracle (`libwasmtime` + `wabt` `wasm-interp` cho GC/typed-ref/eh) |
| `bash tests/diffrt/run_differential.sh <build-dir> 32` | `bash tests/diffrt/run_differential.sh <build-dir> 32` | Pipeline C4 dùng 4 binary C/C++: `diffrt_gen_inputs`, `diffrt_wasm_runner`, `diffrt_transform`, `diffrt_compare` |

## 2. Các lệnh kiểm tra cổng (Gates)

- **Core unit tests + clean-first validator gate**:
  `./build/core_tests/core_tests`
- **ASan runner smoke (BUG A regression — copy_fuse cit_expr union guard)**:
  `cmake --preset macos-asan && cmake --build --preset macos-asan-smoke` (hoặc `ctest --test-dir build/asan -R asan-smoke`) — build toàn tree với AddressSanitizer, install dylib instrumented vào prefix riêng rồi chạy `wasmdec_runner f5 <module> --limit 20`; module mặc định là `wasm-test/gowasm-balls/main.wasm`, fallback in-repo `tests/fixtures/wasmhex_loop_transfers.wasm` (cả hai đều trigger pre-fix)
- **Format / Clang-Tidy / Dep-check**:
  `cmake --build build/rebuild-20261107 --target format-check tidy-check wasmhex_dep_check`
- **P5 Zero Python/Node Gate**:
  `test -z "$(find . -path ./.git -prune -o \( -name '*.py' -o -name '*.pyc' -o -name '*.mjs' -o -name '*.js' \) -print)"`
