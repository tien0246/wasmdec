// G12 fixture "slice" (todo §5 G12 / §3.10): hàm Go có nguồn duyệt slice
// []int32 theo len + index (load i32 từ data pointer) và append (growslice).
package main

// sumSlice cộng các phần tử: mỗi vòng lặp đọc len(xs) rồi load xs[i]
// (i32 tại data + 4*i). append trong main đánh thức growslice runtime.
//
//go:noinline
func sumSlice(xs []int32) int32 {
	total := int32(0)
	for i := 0; i < len(xs); i++ {
		total += xs[i]
	}
	return total
}

func main() {
	xs := []int32{10, 20, 30, 40}
	println(sumSlice(xs))
	xs = append(xs, 50)
	println(sumSlice(xs), len(xs), cap(xs))
}
