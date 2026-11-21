# B1 Golden v1 — KHÔNG promote (regression semantics phát hiện)

Snapshot v1 `/private/tmp/wasmhex-v1` (procs `61617f22…`, post A1-A4 + br_if fix).
Snapshot v0 (baseline chuẩn): `/private/tmp/wasmhex-b1` (procs `c4e1285f…`).

## Kết luận chính

**Build v1 chứa regression semantics nghiêm trọng ở lowering conditional
branch (br_if fix): các nhánh điều kiện bị fold về nhánh fall-through.**
Golden v0 đã được khôi phục làm baseline chính (`tests/golden/` = v0);
bằng chứng v1 đầy đủ tại `/tmp/golden_v1_snapshot` (816 .c + metrics).

## Bằng chứng (Node = semantics thật, v1 = pseudocode build 61617f22)

| Hàm | Node thật | v0 pseudocode | v1 pseudocode |
|---|---|---|---|
| `wasmhex_br_if_result` | (1)=11, (0)=33 | `if (a1!=0) return 11; else return 33;` | `return 33;` |
| `wasmhex_i64_br_if_result` | (1)=44n, (0)=46n | đầy đủ 2 nhánh | luôn 46 |
| `wasmhex_block_branch` | (1)=0, (0)=5 | đầy đủ | `return 5;` |

Family bị ảnh hưởng: mọi hàm có br_if/branch có điều kiện trong corpus —
23 hàm đổi hình dạng v0→v1, đa số là mất nhánh (không phải improvement).

## So sánh tổng quan v0 → v1 (metrics)

| | v0 (c4e1285) | v1 (61617f2) |
|---|---|---|
| ok | 816/843 | 817/843 |
| interr | 27 | 26 |
| Điểm sáng | — | `backend_segment_indirect_loop::dispatch` INTERR→OK (A-fix hiệu quả) |
| Regression | — | conditional-branch fold (br_if) — xem trên |

## Baseline 35 checker trên v1: 25 PASS / 10 fail

Fail thật do regression br_if (7): `p0_regressions` (loop_guard sai),
`p1_regressions`, `differential` (o0 factorial struct-fail),
`golden_merge` (`WASMHEX_GOLDEN_DIFF lines 313→224` — mergeProcessed gãy
cùng bệnh), `duckdb_sample` (629/634 vs 634/634 — 5 fail hidden),
`review_fixes`, `brtable_matrix` (gate jtbl quen thuộc, không liên quan).

Fail giả runner (3): `names`/`p4_idioms`/`prototypes` — runner truyền thiếu
argv[2] → fallback build-dir cứng đã xóa (đã biết từ v0; gọi đúng
`<idat> <build-dir>` thì pass — 3 checker này pass khi gọi đủ args ở v0).

## Nguyên nhân & đề xuất

- A-fix dứt điểm `backend_segment_indirect_loop` ✓ nhưng **br_if fix
  (61617f22) phá choose-branch**: nghiêng về pass DCE/branch-materialize
  xử lý sai điều kiện br_if (fold khi chưa chứng minh dead). Input cho
  Native: 3 bảng trên + `/tmp/golden_v1_snapshot` + log
  `/tmp/checks/base_check_wasmhex_*.log`.
- Sau khi Native land fix: rerun `dump_oracle.py --snapshot <new>` (xóa
  `_metrics.json`) và đối chiếu lại — golden hiện là v0 an toàn.
