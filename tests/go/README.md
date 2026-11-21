# tests/go — fixture Go có nguồn trong repo (G12, todo §5 G12 / §3.10)

Giải mục **3.10** phần Go: "fixture Go nhỏ có nguồn trong repo
(`tests/go/*.go` + lệnh build ghim phiên bản Go), commit `.wasm` kèm
SHA-256" — 5 fixture, mỗi fixture đúng một loại hàm mà nghiệm thu M3 yêu
cầu: **vòng lặp, slice, map, interface call, closure**.

## Bố cục

| Fixture (tests/fixtures/) | Nguồn | Hàm đích (`//go:noinline`) | Dạng |
|---|---|---|---|
| `go_loop.wasm` | `loop/main.go` | `main.sumSquares` | vòng lặp đếm `for i := 1; i <= n; i++` + tích lũy |
| `go_slice.wasm` | `slice/main.go` | `main.sumSlice` | duyệt `[]int32` theo `len` + index load; `append` trong `main` |
| `go_map.wasm` | `map/main.go` | `main.bump` | `map[string]int32` đọc/ghi + `len(map)` |
| `go_iface.wasm` | `iface/main.go` | `main.areaOf` (+ `square.area`, `circle.area`) | gọi qua itab (`call_indirect`), 2 kiểu cụ thể |
| `go_closure.wasm` | `closure/main.go` | `main.adder` (+ `main.adder.func1`) | closure bắt tham số, gọi qua funcval |

Cùng module `wasmhex.test/gofixtures` (`go.mod`: `go 1.27` +
`toolchain go1.27.4`). Hàm đích gắn `//go:noinline` để không bị inline
vào `main`; `main` gọi với đầu vào cố định, deterministic.

## Build

```sh
tests/go/build.sh            # GOOS=js GOARCH=wasm go build -trimpath
```

Toolchain ghim qua `go.mod` (`toolchain go1.27.4`; GOTOOLCHAIN mặc định
tự tải đúng bản nếu máy thiếu). Bản build là **deterministic** trên cùng
toolchain (đã kiểm: rebuild cùng sha). Nếu dùng GOCACHE riêng:
`GOCACHE=<tmp> GOMODCACHE=<tmp> tests/go/build.sh`.

## SHA-256 (go1.27.4 darwin/arm64 Homebrew, -trimpath)

| Fixture | sha256 |
|---|---|
| `go_loop.wasm` | `bed755819fa5833872e2953cf5d53f2585559bfbf9901be38c4b4bd89f630235` |
| `go_slice.wasm` | `e2687bf7d21224ea6230152d690dd5a3a3648a889a409c1d08d977f888e85465` |
| `go_map.wasm` | `ff61c92f13bc0c34fcd18405bbe36626357ac45ce5849a63024a26a462fc322a` |
| `go_iface.wasm` | `378185393c1583728cb8f529dc19e1c2c2fa065a3f1666e9f914bc81696d1eb2` |
| `go_closure.wasm` | `5520f4df476ccc0b3583994dda8f50b1f7eb9d51860c3f0234afce16b68c04f9` |

Wasm trong `tests/fixtures/` là chân lý đã commit; spec tsv có hàng
`valid` cho từng fixture (`tests/fixtures/spec_oracle.tsv`).

## Golden + pseudocode kỳ vọng

Mỗi fixture có `tests/golden/go_<name>/` (bản curated: hàm đích +
`_metrics.json`) và `expected_notes.md` — pseudocode kỳ vọng viết theo
nguồn, kèm đối chiếu PASS/FAIL thực tế và phân loại (engine limit đã
biết / kỳ vọng sai / bug). Quy trình tái tạo golden curated:

```sh
build/<dir>/wasmhex_runner dump-golden go_loop --out-root /tmp/g12
# rồi copy các file hàm đích + _metrics.json vào tests/golden/go_loop/
```
