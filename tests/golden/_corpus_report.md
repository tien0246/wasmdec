# C5 — Corpus Stratified Report

Total: **12 tiers**, generated from `tests/golden/` oracle metrics.

| Tier | Fixtures | Functions | OK | Fail | Pass% | p50ms | p95ms | Top failure reasons |
|---|---|---|---|---|---|---|---|---|
| WAT-generated | 116 | 437 | 434 | 3 | 99.3% | 0 | 3 | code=-31 errea=0x1002×1; code=-31 errea=0x1008×1; code=-31 errea=0x1012×1 |
| Memory64 | 19 | 124 | 103 | 21 | 83.1% | 0 | 3 | code=-31 errea=0x1002×5; code=-31 errea=0x1008×3; code=-31 errea=0x1011×2 |
| Indirect Calls | 26 | 76 | 76 | 0 | 100.0% | 0 | 3 | — |
| Atomics | 3 | 71 | 70 | 1 | 98.6% | 0 | 1 | code=-31 errea=0x1002×1 |
| Linking | 111 | 39 | 39 | 0 | 100.0% | 2 | 3 | — |
| SIMD | 36 | 36 | 35 | 1 | 97.2% | 0 | 5 | code=-31 errea=0x1004×1 |
| Compiled C/C++ | 2 | 23 | 23 | 0 | 100.0% | 2 | 4 | — |
| GC / Reference Types | 60 | 18 | 18 | 0 | 100.0% | 1 | 3 | — |
| br_table matrix | 11 | 11 | 11 | 0 | 100.0% | 3 | 82 | — |
| DWARF | 4 | 6 | 6 | 0 | 100.0% | 7 | 13 | — |
| Component Model | 7 | 2 | 2 | 0 | 100.0% | 2 | 2 | — |
| Exception Handling | 24 | 0 | 0 | 0 | — | 0 | 0 | — |

**Grand total: 817/843 functions OK (96.9%)**

