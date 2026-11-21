// G12 fixture "map" (todo §5 G12 / §3.10): hàm Go có nguồn truy cập
// map[string]int32 — lookup + assign qua runtime mapaccess/mapassign
// (bản faststr cho khóa string), kèm len(map).
package main

// bump tăng bộ đếm của khóa k lên 1 và trả về giá trị mới:
// m[k] đọc (mapaccess1_faststr, ok-form không dùng) và m[k] = … ghi
// (mapassign_faststr). Cả hai gọi runtime có chuỗi (hash, key {ptr,len}).
//
//go:noinline
func bump(m map[string]int32, k string) int32 {
	m[k] = m[k] + 1
	return m[k]
}

func main() {
	m := map[string]int32{}
	n := bump(m, "alpha")
	n += bump(m, "beta")
	n += bump(m, "alpha")
	println(n, len(m))
}
