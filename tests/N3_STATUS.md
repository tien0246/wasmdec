# N3: Bỏ Python khỏi test — Kế hoạch & Trạng thái

## N3-1: idalib C++ Runner — **DEFERRED (SIGSEGV)**

| File | Trạng thái |
|---|---|
| `tests/runner/idalib_test.cpp` | ✅ Build OK (arm64_mac_64 libidalib.dylib), **SIGSEGV runtime** trên `open_database(.wasm)` — wasmhex processor chưa tương thích idalib-mode auto-analysis. D10 fix chưa cover đường này. |
| `tests/runner/wasmhex_dump.cpp` | Cấu trúc sẵn (không build được — API mismatch). Chờ idalib tương thích. |

**Runner hiện tại (idat + oracle_ida.py) vẫn hoạt động tốt** — N3-1 là nâng cấp,
không bắt buộc. Golden runner C1 (C++ điều phối + idat worker) đã chứng minh
sufficient.

## N3-2: expect.txt format — **DONE (template)**

| File | Trạng thái |
|---|---|
| `tests/golden/bt_small/expect.txt` | ✅ Template đầu tiên: contains/not_contains/status |
| `tests/golden/run_expect.py` | ✅ Runner đọc expect.txt → PASS/FAIL. Đã test: `EXPECT_PASS bt_small (5 expectations)` |

**Chuyển các checker còn lại**: mỗi fixture chỉ cần 1 file expect.txt (5-15 dòng)
thay vì 1 script Python riêng (~100 dòng). 86 script check_*.py + verify_*.py
có thể chuyển dần — mỗi cái 10 phút công việc cơ khí.

## N3-3: DiffRT C++ — **DONE (2/3 tools)**

| File | Trạng thái |
|---|---|
| `tests/diffrt/compare.cc` | ✅ Build + test PASS — thay thế compare.py hoàn toàn |
| `tests/diffrt/gen_inputs.cc` | ✅ Build + test PASS — type-aware random + boundary |
| `tests/diffrt/transform.cc` | ❌ Chưa port (regex-heavy — cần C++ regex hoặc string ops) |

## N3-4: Fixture Generator C++ — **DONE (concept proven)**

| File | Trạng thái |
|---|---|
| `tools/gen_fixtures.cc` | ✅ Build + chạy OK — sinh 3 br_table WAT + compile wat2wasm. Concept proven. 2 generator còn lại (dwarf, differential) cùng pattern. |

## N3-5: Xóa Python — **CHƯA THỂ (chờ N3-1)**

### Có thể xóa NGAY (C++ replacement sẵn):

```
tests/diffrt/compare.py       → compare.cc sẵn
tests/diffrt/gen_inputs.py    → gen_inputs.cc sẵn
tests/corpus_report.py        → tool 1 lần (đã generate output)
tests/support/ida.py          → golden_runner C++ thay thế
```

### Phải GIỮ (chờ N3-1 idalib hoặc expect.txt migration):

```
tests/golden/oracle_ida.py        → Cần IDAPython (idalib chưa work)
tests/golden/dump_oracle.py       → Điều phối oracle_ida.py
tests/golden/run_expect.py        → Đã là data-driven (chấp nhận tạm)
tests/diffrt/transform.py         → Chưa port C++ (N3-3 incomplete)
tests/diffrt/run_differential.sh  → Điều phối cả .py và .cc
tests/integration/ida/*.py (86)   → Chờ expect.txt migration từng cái
tests/fixtures/gen/*.py (3)       → Chờ C++ port đầy đủ (N3-4 mở rộng)
```

### Lộ trình sạch Python:

1. Native Lead fix idalib compat → port oracle_ida.py → C++
2. Chuyển 86 checker sang expect.txt (cơ khí, 10 phút/cái)
3. Port transform.py + gen fixtures → C++
4. `find . -name '*.py' -not -path './.git/*' | xargs rm`
