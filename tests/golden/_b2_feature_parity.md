# B2 — Bảng tương đương tính năng native C++ vs Python pipeline

Điều kiện tiên quyết cho B4 (xóa Python). Phương pháp: `rg` từng mục trong
`native/wasmhex/` vs `python/wasmhex/` + checker cover + oracle v2 (817/843
hàm decompile OK trên 419 fixtures — breadth proof).

## Bảng chính

| # | Mục | Native C++ | Python | Checker chứng minh | Đề xuất cho B4 |
|---|---|---|---|---|---|
| 1 | call_indirect finite targets (sparse dispatch) | ✅ `lower_calls.cpp` (17 refs) | ✅ `native_lifter.py` (mcases) | `check_wasmhex_sparse_*` (3) + `call_ref_multi` (5) | **Giữ native** — python trùng chức năng |
| 2 | table.copy/set | ✅ `loader/entry.cpp` (5 refs elem/copy) | ✅ `lowering/prepare.py` (4 refs) | `check_wasmhex_table_size` + P0 | **Giữ native** |
| 3 | Segment-driven targets (elem active, offset động) | ✅ `core/module.cpp` (39 refs elem) + `P4-2 __table_base→fn-ptr` (todo) | ✅ native_lifter | `check_wasmhex_imported_active_table` | **Giữ native** |
| 4 | EH legacy try/catch/throw + try_table | ✅ `lower_exception.cpp` (25) + `entry.cpp` (9) | ✅ metapc_lifter | `check_wasmhex_legacy_{direct_throw,call_unwind}` (2) + `f5_try_table` fixtures | **Giữ native** |
| 5 | GC/i31 (ref types, struct/array.new) | ✅ `lower_reference.cpp` (6) | ✅ | `check_wasmhex_names` + `typed_ref` fixtures + P0 | **Giữ native** |
| 6 | Multi-value returns | ✅ `lower_calls.cpp` (16 tuple refs) | ✅ | `check_wasmhex_calls_multi_exit` + `call_ref_multi` | **Giữ native** |
| 7 | SIMD v128 | ✅ `lower_simd.cpp` (33) | ✅ | `check_wasmhex_p0_regressions` (v128 sret/lane) + fixtures `simd_*` | **Giữ native** |
| 8 | DWARF names | ✅ `loader/dwarf.cpp` (P5-1 land) | ❌ | `check_wasmhex_dwarf_names` (5 markers) | **Giữ native** (python không có) |
| 9 | Component model | ✅ `loader/entry.cpp` (6 refs, reject unsupported) | ✅ `component/` parser riêng | `check_component_loader_idat` | **Giữ native** (python chỉ parser/valid, native đã reject đúng) |
| 10 | Linker `.o.wasm` | ❌ | ✅ `link/` | unit tests `test_wasm_link_*` (529 pytest) | **Bỏ theo todo B4** — không nhu cầu thật cho decompiler |
| 11 | MetaPC F5 bridge | N/A (native là primary) | ✅ `metapc_lifter.py` | `verify_f5_*` (metapc probes) | **Bỏ** — fallback cho license không PLFM_WASM; native chạy trực tiếp |
| 12 | PLFM_WASM native-lifter | N/A | ✅ `native_lifter.py` | trùng #1-#7 | **Bỏ** — trùng chức năng native C++ |

## Tóm tắt khuyến nghị B4

**Bỏ (3)**: #10 linker `.o.wasm`, #11 metapc bridge, #12 native-lifter python
— cả 3 không cần cho decompiler native; metapc bridge chỉ dùng khi license
không có PLFM_WASM (đã xác nhận trên máy này license có native).

**Giữ native (9)**: #1-#8 đều đã có C++ land + checker xanh (33/35 baseline
v2 → 34/35 snapshot v3). Oracle v2 chứng minh breadth: 817/843 hàm OK trên
419 fixtures, 26 interr đều fail-closed chủ ý (memory64 shared/exported
simulated).

**Cần port (0)**: mọi mục python có → native đã có tương đương.
