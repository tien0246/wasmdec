# B1 Integration baseline (frozen oracle snapshot)

Snapshot `/private/tmp/wasmhex-b1` (procs `c4e1285f…`, built 2026-10-30 23:47) —
default pipeline (chain + hidden bounds).

**32/35 check_*.py PASS** — tổng 1041s. build_wasmhex_typed_ref_unsupported_gc.py exit 0.

| Checker | Exit | giây | Marker |
|---|---|---|---|
| check_component_loader_idat | 0 | 3 | WASMHEX_COMPONENT_IDAT_REJECTIONS_OK |
| check_wasmhex_brtable_matrix | 2 | 69 | (see log) |
| check_wasmhex_call_ref_multi | 0 | 11 | WASMHEX_CALL_REF_NONNUMERIC_PARAMETERS_REJECTED |
| check_wasmhex_call_ref_rejections | 0 | 4 | WASMHEX_CALL_REF_REJECTIONS_OK |
| check_wasmhex_calls_multi_exit | 0 | 1 | WASMHEX_HEADLESS_EXIT_OK |
| check_wasmhex_differential | 0 | 3 | WASMHEX_DIFF_OK |
| check_wasmhex_duckdb_sample | 0 | 387 | WASMHEX_P74_OK max_failures=0  |
| check_wasmhex_dwarf_names | 0 | 8 | WASMHEX_DWARF_OK |
| check_wasmhex_fixed_memory64 | 0 | 23 | WASMHEX_FIXED_MEMORY64_INVALID_REJECTED wasmhex_fixed_memory |
| check_wasmhex_fixed_memory_proof | 0 | 23 | WASMHEX_FIXED_MEMORY_REJECTED wasmhex_fixed_memory_lane_inva |
| check_wasmhex_fixed_memory_scalar | 0 | 6 | WASMHEX_FIXED_MEMORY_SCALAR_CASE_OK wasmhex_memory64.wasm |
| check_wasmhex_golden_merge | 0 | 159 | WASMHEX_GOLDEN_OK lines=313 stack=0 helpers=5 coerce=0 goto= |
| check_wasmhex_growable_memory32 | 0 | 22 | WASMHEX_GROWABLE_MEMORY32_REJECTED multi |
| check_wasmhex_growable_memory64 | 0 | 15 | WASMHEX_GROWABLE_MEMORY64_REJECTED multi |
| check_wasmhex_i64_select_shift | 0 | 3 | WASMHEX_I64_SELECT_SHIFT_F5_OK |
| check_wasmhex_imported_active_table | 0 | 30 | WASMHEX_IMPORTED_ACTIVE_TABLE_REJECTED bad_offset |
| check_wasmhex_large_br_table | 0 | 2 | WASMHEX_LARGE_BR_TABLE_F5_OK |
| check_wasmhex_legacy_call_unwind | 0 | 17 | WASMHEX_LEGACY_CALL_UNWIND_FLOAT_F5_OK float __usercall caug |
| check_wasmhex_legacy_direct_throw | 0 | 12 | WASMHEX_LEGACY_DIRECT_THROW_REJECTED i64_multi |
| check_wasmhex_m7a_explicit | 0 | 3 | WASMHEX_M7A_EXPLICIT_OK |
| check_wasmhex_m7ab_explicit | 0 | 2 | WASMHEX_M7AB_EXPLICIT_OK |
| check_wasmhex_m7b_explicit | 0 | 2 | WASMHEX_M7B_EXPLICIT_OK |
| check_wasmhex_names | 0 | 3 | (see log) |
| check_wasmhex_native_operand_validation | 0 | 7 | WASMHEX_NATIVE_OPERAND_VALIDATION_OK |
| check_wasmhex_p0_regressions | 0 | 8 | WASMHEX_P0_REGRESSIONS_OK |
| check_wasmhex_p1_regressions | 1 | 170 | WASMHEX_P5_2_FAIL_CLOSED_OK conflict_width_mix |
| check_wasmhex_p4_idioms | 0 | 2 | (see log) |
| check_wasmhex_prototypes | 0 | 2 | (see log) |
| check_wasmhex_repeated_scalar_loads | 0 | 2 | WASMHEX_REPEATED_SCALAR_LOADS_F5_OK |
| check_wasmhex_review_fixes | 1 | 11 | WASMHEX_REVIEW_F3_F5_OK |
| check_wasmhex_sparse_large_indirect | 0 | 3 | WASMHEX_SPARSE_LARGE_CASE_LIMIT_REJECTED |
| check_wasmhex_sparse_multi_indirect | 0 | 5 | WASMHEX_SPARSE_MULTI_REFERENCE_REJECTED |
| check_wasmhex_sparse_reference_multi | 0 | 8 | WASMHEX_SPARSE_REFERENCE_MULTI_REJECTED two_handles |
| check_wasmhex_stack_indirect_abi | 0 | 9 | WASMHEX_STACK_INDIRECT_ABI_F5_OK |
| check_wasmhex_table_size | 0 | 6 | WASMHEX_TABLE_SIZE_OUT_OF_RANGE_REJECTED |

## Phân loại fail (3 checker)

- **check_wasmhex_p1_regressions**: `conflict_width_mix` → `INTERR 50709` trên citem `(void)a1 / (void)a2` — đúng family div_u/rem_u mà Native đang fix (A1-A4); bằng chứng đóng băng tại snapshot giữa chừng.
- **check_wasmhex_brtable_matrix**: exit 2 theo thiết kế gate — cells jtbl `JUMPOUT` (Native đang fix; chú thích: `bt_depth_per_case_64 jtbl` đã OK trên snapshot này, chỉ còn bảng phẳng).
- **check_wasmhex_review_fixes**: assert `wasm_select_ref(` không còn — kiến trúc select-ref đang đổi giữa chừng (A1-A4); stale-assert so với snapshot.

## Ghi chú coverage

- 78 `verify_*.py` là script in-IDA đã được các check_* tiêu thụ hoặc đã rà ở các round trước — không chạy độc lập ở đây.
- Duckdb full-module oracle **chưa dump** (module 347MB DB); golden `mergeProcessed` riêng đã có `tests/integration/golden/` + checker — tham chiếu B1 cho phần duckdb cho round sau.
- 3 checker names/p4_idioms/prototypes có fallback build-dir cứng (snapshot tmp cũ đã xóa) — runner phải truyền đủ `<idat> <build-dir>`; đã ghi vào cách rerun.
