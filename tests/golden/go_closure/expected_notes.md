# G12 "closure" — pseudocode kỳ vọng vs dump thực tế

Nguồn: `tests/go/closure/main.go` (go1.27.4). Dump: `main.adder.c`,
`main.adder.func1.c`, `main.main.c` (module-wide 1251/1251 OK, 0 INTERR).

## Kỳ vọng (dạng hiển thị hiện tại)

```c
// func adder(n int32) func(int32) int32 — dựng closure bắt n
int __usercall main.adder(int a1@<r0>)
{
  SP -= 32;
  *(u64*)SP = 16;                    // kích thước cấp phát (closure object)
  *(u64*)(SP+8) = <no-scan type word>;
  *(u8*)(SP+16) = 1;                 // cờ small-object
  runtime.mallocgc…();               // go1.27: mallocgcSmallNoScanSC2
  v_obj = *(u64*)(SP+22);
  *(u64*)v_obj        = <funcval của main.adder.func1>;
  *(i32*)((u8*)v_obj + 6) = *(i32*)(SP+38);   // capture n (ô tham số cha)
  *(u64*)(SP+48) = v_obj;            // ô kết quả: closure (TODO engine)
  SP += 40;
  return 0;
}

// func(x int32) int32 — thân closure: main.adder.func1
*(i32*)(SP+16) = *(i32*)(SP+6)                     // x (tham số)
               + *(i32*)(ctx + 6);                 // n (biến bắt) — ctx = funcref

// main: f := adder(7); f(5)
v_f = <kết quả adder>;
if ( v_f == 0 ) runtime.sigpanic();       // nil funcval check
v_fn = *(fn**)v_f;                        // load funcval
*(i32*)(SP+0) = 5;                        // đối số x
ctx_reg = v_f;                            // context = closure
v_fn();                                  // gọi gián tiếp
```

Ghi chú kỳ vọng: closure object = cấp phát + funcval + capture field;
thân closure là hàm riêng đọc biến bắt qua thanh ghi context; call site
= load funcval → nil check → set context → call gián tiếp. Đối số/ kết
quả vẫn qua ô stack (G4/G5 — dạng hiện tại).

## Thực tế

`main.adder`: frame −32, đẩy {16, 874936, 1} → `runtime.mallocgcSmallNoScanSC2`
→ `v3 = *(u64*)(SP+22)`; `*(u64*)v3 = 350355456` (funcval);
`*(i32*)(v3+6) = *(i32*)(SP+38)` (capture n — đọc ô tham số cha);
kết quả `*(u64*)(SP+48) = v3`. `main.adder.func1`:
`*(i32*)(SP+16) = *(i32*)(SP+6) + *(i32*)(wasmhex_global_1 + 6)` — đúng
x + n với n qua thanh ghi context. `main.main`: hai chuỗi nil-check →
load funcval → set arg (7/−1, 5/10) → `wasmhex_global_1 = v_f` → gọi
gián tiếp → printint.

## Bảng PASS/FAIL

| Kỳ vọng | Kết quả | Phân loại |
|---|---|---|
| Cấp phát closure + ghi funcval + capture n | ✅ PASS | — |
| Thân closure đọc biến bắt qua context reg | ✅ PASS | — |
| Call site: load funcval + nil check + call gián tiếp | ✅ PASS | — |
| Hai chuỗi adder/gọi/print đúng thứ tự nguồn | ✅ PASS | — |
| 0 dispatcher / 0 morestack guard | ✅ PASS | — |
| Closure hiện thành giá trị hàm C (trả con trỏ hàm có capture) | ❌ FAIL | engine limit đã biết (G4 giá trị hóa — còn mở) |
| `f(5)` hiện đối số theo giá trị | ❌ FAIL | engine limit đã biết (G5 — còn mở) |

Kết luận: **PASS cấu trúc hoàn toàn**; không có bug mới.
