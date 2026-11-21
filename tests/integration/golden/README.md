# Golden baseline — `duckdb::TDigest::mergeProcessed` (P7-1)

Mục tiêu P7-1: lưu output F5 hiện tại làm baseline; sau mỗi milestone lưu lại
và diff. File `mergeProcessed_baseline.txt` là pseudocode freeze cùng 6 metrics
trong header `# METRICS`.

## Metrics

| Metric | Định nghĩa |
|---|---|
| `lines` | Số dòng pseudocode không trống |
| `stack` | Số occurrence `STACK[` |
| `helpers` | Số token `wasmhex_*` / `wasm_*` (regex `\b(?:wasmhex_\|wasm_)\w+`) |
| `coerce` | Số occurrence `COERCE_` |
| `goto` | Số statement `goto` (`\bgoto\b`) |
| `lvars` | Số lvar Hex-Rays (báo cáo từ trong IDA, không parse text) |

## Chạy / regenerate

```bash
# So sánh với baseline (chấm đỏ nếu metric nào đổi):
build/rebuild-20261107/wasmhex_runner golden-merge

# Ghi lại baseline mới sau khi milestone được duyệt:
build/rebuild-20261107/wasmhex_runner golden-merge --regenerate

# Bắt buộc so văn bản byte-đúng (mặc định chỉ so metrics — text có thể dao
# động nhỏ theo IDA version):
build/rebuild-20261107/wasmhex_runner golden-merge --strict
```

Điều kiện decompile giống P1 checker: hidden bounds mode (mặc định),
`ARG_HINTS_MODE=0` (H7: bật cfunc cache mặc định; chỉ dùng `DECOMP_NO_CACHE` khi đo).

## Dòng thời gian số dòng (tổng dòng pseudocode, lịch sử các mốc)

| Mốc | lines (tổng) | Ghi chú |
|---|---|---|
| Trước P1 | 700+ | spill loạn, STACK[ nhiều |
| P1 giữa chừng | 526 | |
| P1 muộn | 374 | |
| P2 | 356 → 355 | |
| P5 | 330 | |
| P6/M7 (baseline này) | **314** (313 non-empty) | stack=0, coerce=0, helpers=5, goto=13, lvars=64 |

Sau mỗi milestone: chạy checker (đỏ nếu lệch), Root duyệt thay đổi, rồi
regenerate để ghi mốc mới.
