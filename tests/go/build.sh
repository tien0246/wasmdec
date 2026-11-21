#!/bin/bash
# G12 (todo §5 G12 / §3.10): build 5 fixture Go có nguồn trong repo.
#
# Toolchain ghim: go.mod (`go 1.27` + `toolchain go1.27.4`). Bản sha256
# tương ứng ghi trong README.md; build lại trên toolchain khác cho phép
# khác byte — sha256 trong README là chân lý của fixture đã commit.
#
# Yêu cầu: go >= 1.27 (darwin/arm64 Homebrew hoặc bất kỳ; GOTOOLCHAIN=auto
# sẽ tự tải đúng toolchain line nếu thiếu).
set -euo pipefail
cd "$(dirname "$0")"

out="../fixtures"
mkdir -p "$out"

for name in loop slice map iface closure; do
  GOOS=js GOARCH=wasm go build -trimpath -o "$out/go_${name}.wasm" "./$name"
done

echo "sha256:"
(cd "$out" && shasum -a 256 go_loop.wasm go_slice.wasm go_map.wasm go_iface.wasm go_closure.wasm)
