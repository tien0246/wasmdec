# G12 "iface" — pseudocode kỳ vọng vs dump thực tế

Nguồn: `tests/go/iface/main.go` (go1.27.4). Dump: `main.areaOf.c`,
`main.square.area.c`, `main.circle.area.c`, `main.main.c`
(module-wide 1254/1254 OK, 0 INTERR).

## Kỳ vọng (dạng hiển thị hiện tại)

```c
// func areaOf(s shape) float64 — gọi qua itab
void __usercall main.areaOf(int a1@<r0>)
{
  v_itab  = *(u64*)(SP + 30);   // s.tab  (ô tham số +8)
  v_data  = *(u64*)(SP + 38);   // s.data (ô tham số +16)
  if ( v_itab == 0 ) runtime.sigpanic();          // nil interface check
  (*(fn**)(v_itab + 22))(v_data);                 // itab->area(): slot 22
  // kết quả double ghi tại ô kết quả của caller (TODO engine: x=f)
}

// func (s square) area() float64 — main.square.area
*(double*)(SP + 16) = *(double*)(SP + 6) * *(double*)(SP + 6);  // side*side

// main:println(areaOf(square{2})) — dựng {type word, &box(2.0)} rồi gọi
```

Ghi chú kỳ vọng: lời gọi interface phải hiện là **call gián tiếp qua slot
phương thức của itab** (không phải call trực tiếp, không phải protocol
PC_F thô — G4 đã hạ protocol call_indirect); receiver là data word;
nil check hiện sigpanic (dạng hiện tại); 2 method cụ thể tách bạch với
đúng phép tính của riêng chúng (side*side vs 3*r*r).

## Thực tế

`main.areaOf`: nil check `if (v2 == 0) runtime.sigpanic()` rồi
`(*(void (__usercall **)(__int64@<r0:r1>))&byte_2[(u32)v2 + 22])(v3);` —
đúng load con trỏ hàm tại itab+22, gọi với receiver v3.
`main.square.area`: `*(double*)(SP+16) = *(double*)(SP+6) * *(double*)(SP+6);`
(kèm comment method `go: (main.square) area(...)` — M0-B).
`main.circle.area`: `3 * r * r` (hằng 3 fold đúng). `main.main`: hai lần
dựng {type word, data} khác nhau (905000/46808 và 905032/46816) → gọi
`main.areaOf` trực tiếp → đọc kết quả double → `runtime.printfloat64`.

## Bảng PASS/FAIL

| Kỳ vọng | Kết quả | Phân loại |
|---|---|---|
| Call qua slot itab (+22), receiver = data word | ✅ PASS | — |
| Không còn dư vết protocol PC_F của call_indirect | ✅ PASS | — |
| Nil interface check (sigpanic, dạng hiện tại) | ✅ PASS | — |
| 2 method cụ thể đúng phép tính riêng | ✅ PASS | — |
| main: 2 lần box {type word, data} + gọi areaOf | ✅ PASS | — |
| `s.area()` hiện thành 1 biểu thức C | ❌ FAIL | engine limit đã biết (call-indirect qua bảng — bản chất) |
| Kết quả `return s.area()` / x=f | ❌ FAIL | declined engine (x=f pending) |

Kết luận: **PASS cấu trúc hoàn toàn** — đây là dạng hiển thị trung thực
nhất có thể cho itab dispatch trên C; không có bug mới.
