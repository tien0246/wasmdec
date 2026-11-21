// G12 fixture "loop" (todo §5 G12 / §3.10): một hàm Go có nguồn,
// vòng lặp đếm thuần — dùng làm ground truth cho pseudocode kỳ vọng.
//
// Build: xem ../build.sh (GOOS=js GOARCH=wasm, go1.27.x — ghim trong ../go.mod).
package main

// sumSquares trả về 1²+2²+…+n² bằng vòng lặp đếm (for i := 1; i <= n; i++),
// thân vòng có số học (i*i) và biến tích lũy (total).
//
//go:noinline
func sumSquares(n int32) int32 {
	total := int32(0)
	for i := int32(1); i <= n; i++ {
		total += i * i
	}
	return total
}

func main() {
	println(sumSquares(10))
	println(sumSquares(100))
	println(sumSquares(0))
}
