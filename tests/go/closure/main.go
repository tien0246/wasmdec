// G12 fixture "closure" (todo §5 G12 / §3.10): closure Go bắt biến —
// adder trả về closure bắt tham số n (funcval + môi trường bị bắt),
// lời gọi closure đi qua funcval.
package main

// adder trả về closure func(int32) int32 bắt tham số n theo giá trị.
// Thân closure là hàm wasm riêng (main.adder.func1) nhận context
// funcref + capture; add7(5) gọi qua funcval của kết quả.
//
//go:noinline
func adder(n int32) func(int32) int32 {
	return func(x int32) int32 {
		return x + n
	}
}

func main() {
	add7 := adder(7)
	println(add7(5))
	println(adder(-1)(10))
}
