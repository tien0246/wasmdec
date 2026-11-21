# B1 Golden v2 — BASELINE CHÍNH THỨC (promoted)

Snapshot v2 `/private/tmp/wasmhex-v2` (procs `0ecdded…`, post A1-A4 +
br_if-fix). Đây là hành vi decompiler chính thức: golden `tests/golden/`
hiện là v2.

## So sánh v0 → v1(corrupt) → v2

| | v0 (`c4e1285`) | v1 (`61617f2`, rejected) | **v2 (`0ecdded`)** |
|---|---|---|---|
| ok / interr | 816 / 27 | 817 / 26 | **817 / 26** |
| Regression | — | conditional-branch fold (mất nhánh) | **ĐÃ SỬA** — mọi branch 2 nhánh |
| dispatch (A-fix) | INTERR | OK | **OK (giữ)** |
| mergeProcessed | 313 lines | 224 (gãy) | **342 lines** (+29 = A1 `__THREW__` materialize stores giữ — intentional) |
| duckdb 1/100 | 634/634 | 629/634 | **634/634 cả 2 mode** |
| Baseline 35 checker | 32/35 | 25/35 | **33/35** (xem phân loại) |

## 3-spot branches (bằng chứng v2 đúng ngữ nghĩa — Node khớp)

- `wasmhex_br_if_result`: `if (a1 != 0) return 11; else return 33;` ✓ (Node 11/33)
- `wasmhex_i64_br_if_result`: 2 nhánh 44/46 ✓
- `wasmhex_block_branch`: `if (a1 == 0) return 5; return 0;` ✓ (Node 0/5)

## Baseline 35 checker trên v2: 33 PASS / 2 fail

1. `check_wasmhex_brtable_matrix` exit 2 — gate jtbl `JUMPOUT` (đã biết,
   Native đang fix jtbl lowering; không phải regression v2).
2. `check_wasmhex_p1_regressions` — `div_trap_drop` **INTERR 50709**
   (cot_sdiv citem family): hidden `i32.div_s; drop` (trap side-effect bị
   drop) gãy decompile — fail code-side còn tồn đọng của A-div change,
   bàn giao Native (log `/tmp/checks/base_check_wasmhex_p1_regressions.log`).

`check_wasmhex_review_fixes` đã PASS sau khi cập nhật 3 stale-assert cho
hành vi P6-1/P6-4 chính thức (typed select → `a1 ? a2 : a3`; float names
`fminimumf/roundevenf/fmaximum` — giữ nguyên tinh thần guard: đúng 1 deref
mỗi argument, không helper `_bits`).

## Ghi chú promote

- `tests/golden/` = v2 (regenerated từ snapshot v2); v0 official backup
  `/tmp/golden_v0_official`; v1-corrupt evidence `/tmp/golden_v1_snapshot`.
- `tests/integration/golden/mergeProcessed_baseline.txt` regenerated:
  `lines=342 stack=0 helpers=5 coerce=0 goto=13 lvars=64`.
- Rerun sau milestone: `python3 tests/golden/dump_oracle.py --idat <idat>
  --snapshot <build>` (xóa `_metrics.json` cũ).
