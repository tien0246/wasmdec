# CORPUS.md — manifest corpus chính thức (V2, todo §10)

Sáu corpus lớn dưới đây là đầu vào đối chứng của mọi sweep/hash pin trong
`tests/golden/_t0_freeze.md` và `docs/PERF_BASELINE.md`. Cả cây `wasm-test/`
bị git-ignore (duckdb một mình đã 37 MB), nên tính nguyên vẹn được ép bằng
bảng machine-readable `tests/corpus/manifest.tsv` + check `tests/corpus/check_corpus.cmake`
(ctest `corpus-check`, đăng ký ở root `CMakeLists.txt`):

```sh
cmake -D WASMHEX_CORPUS_MANIFEST=tests/corpus/manifest.tsv -P tests/corpus/check_corpus.cmake
# hoặc qua ctest:  ctest --test-dir build/core_tests -R corpus-check
```

**Quy tắc V2: thiếu corpus = fail.** Một corpus thiếu/hỏng làm mọi sweep-hash
pin đi kèm mất giá trị đối chứng — check fail ngay tại dòng tương ứng kèm gợi
ý tái tạo, không bao giờ pass rỗng.

| # | Corpus | Đường dẫn | SHA-256 | Toolchain | Vai trò |
|---|---|---|---|---|---|
| 1 | `duckdb` | `wasm-test/wasm-test/duckdb.wasm` | `09c6ecb8…804365` | Emscripten (DuckDB WASM shell) | đối chứng F5 lớn C++ (sample sweep 634/640 hàm, bar chính `5f728cb0…`/`0e464bfd…`) |
| 2 | `main126` | `wasm-test/wasm-test/main.wasm` | `a39d9bc1…89a96e` | Go `go1.26.5`, name-stripped | corpus đối chứng Go toolchain 1.26 (full sweep 3.255 hàm, pin `4d333ba8…`) |
| 3 | `gowasm-balls` | `wasm-test/gowasm-balls/main.wasm` | `c59cf3b5…db20c` | Go `go1.27.3`, named | corpus Go toolchain 1.27 (full sweep 1.759 hàm, pin `36d4127a…`) |
| 4 | `gowasm-ballclone` | `wasm-test/gowasm-ballclone/main.wasm` | `727f9de5…196d9` | Go `go1.27.3` | fixture M0-B (todo 3.4 + 3.7: struct trùng tên qua 2 IDB; sweep 1.618 hàm, pin `b3741370…`) |
| 5 | `gowasm126` | `wasm-test/gowasm126/main.wasm` | `e31d7c7c…9aa3e` | Go `go1.26.5` (`GOTOOLCHAIN`) | fixture rebuild G7-S1 (go1.26.5 tái tạo được từ nguồn; sweep 1.794 hàm, pin `604af852…`) |
| 6 | `musl-00050c6a` | `wasm-test/00050c6a.wasm` | `e7c14f96…b95b` | Emscripten/musl (printf/MD5/soft-float128, có name section) | corpus Emscripten thật đầu tiên (CTF challenge user thả 17/11; 102 hàm; tái hiện lớp từ chối entry-global.get T0 rev40 — 43/102 refused pre-fix → 0 post-fix; pin F5-subset `b5a7fbeb…`) |

SHA-256 đủ và số byte chính xác nằm trong `manifest.tsv` (đo trực tiếp từ
file hiện diện; README của ballclone ghi size 2.486.760 là số cũ — SHA khớp
nên bảng manifest là chuẩn).

## Tái tạo từng corpus

1. **duckdb** — binary do Root cung cấp khi dựng baseline 12/11; upstream là
   bản shell WASM của DuckDB (dự án duckdb-web). Bản build upstream chính xác
   không re-pin được sau thực tế — corpus được neo bằng SHA-256 ở manifest.
   Đường phục vụ đối chứng: sample sweep stride (`wasmdec_runner f5 <path>
   --stride N`) và bar F5-subset `5f728cb0…` (DECISIONS #23).
2. **main126** — binary do Root cung cấp (biến thể name-stripped của layout
   nguồn `gowasm126`). Không build lại từ nguồn trong repo; neo bằng SHA-256.
3. **gowasm-balls** — nguồn `wasm-test/gowasm-balls/main.go` (từ
   `github.com/eveperc/go-wasm-example`):
   ```sh
   cd wasm-test/gowasm-balls
   GO111MODULE=off GOOS=js GOARCH=wasm go build -o main.wasm ./main.go   # go1.27.3 darwin/arm64 Homebrew
   ```
   Chi tiết + baseline: `wasm-test/gowasm-balls/BASELINE.md`.
4. **gowasm-ballclone** — nguồn `wasm-test/gowasm-ballclone/main.go` (viết
   riêng cho M0-B):
   ```sh
   cd wasm-test/gowasm-ballclone
   GO111MODULE=off GOOS=js GOARCH=wasm go build -o main.wasm ./main.go   # go1.27.3 darwin/arm64 Homebrew
   ```
   Chi tiết: `wasm-test/gowasm-ballclone/README.md`.
5. **gowasm126** — nguồn `wasm-test/gowasm126/{main.go,go.mod}` (G7-S1):
   ```sh
   cd wasm-test/gowasm126
   GOOS=js GOARCH=wasm GOTOOLCHAIN=go1.26.5 go build -o main.wasm .
   ```
6. **musl-00050c6a** — binary CTF (Emscripten/musl) do user thả
   `wasm-test/00050c6a.wasm` ngày 17/11 (không có nguồn để rebuild);
   neo bằng SHA-256. Vai trò: corpus Emscripten thật đầu tiên — tái hiện
   đúng lớp lỗi entry-global.get bị vá ở T0 rev40 (43/102 hàm bị từ chối
   F5 -31 trước vá, 0 sau vá; bar `b5a7fbeb…`).

Lưu ý: Go rebuild deterministic trên cùng toolchain — các lệnh trên tái tạo
đúng SHA đã pin (cùng tiền lệ `tests/go/build.sh`, T0 rev33 ghi chú 1).
Toolchain khác phiên bản sẽ ra binary khác — khi đó phải re-pin manifest +
sweep lại có chủ đích, không phải sửa lặng lẽ hash.
