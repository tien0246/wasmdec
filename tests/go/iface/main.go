// G12 fixture "iface" (todo §5 G12 / §3.10): lời gọi qua interface Go —
// itab dispatch thành call_indirect; hai kiểu cụ thể (square, circle) cho
// hai itab khác nhau, receiver là giá trị (box vào từ dữ liệu interface).
package main

type shape interface {
	area() float64
}

type square struct{ side float64 }

//go:noinline
func (s square) area() float64 { return s.side * s.side }

type circle struct{ radius float64 }

//go:noinline
func (c circle) area() float64 { return 3 * c.radius * c.radius }

// areaOf gọi qua interface: load itab từ từ dữ liệu interface rồi gọi
// phương thức qua bảng hàm của itab (call_indirect trên wasm).
//
//go:noinline
func areaOf(s shape) float64 {
	return s.area()
}

func main() {
	println(areaOf(square{side: 2}))
	println(areaOf(circle{radius: 3}))
}
