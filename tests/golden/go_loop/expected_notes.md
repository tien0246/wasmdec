# G12 "loop" — pseudocode kỳ vọng vs dump thực tế

Nguồn: `tests/go/loop/main.go` (go1.27.4). Dump: `main.sumSquares.c` +
`main.main.c` (curated; module-wide 1250/1250 OK, 0 INTERR —
`_metrics.json`). Fixture sha256: xem `tests/go/README.md`.

## Kỳ vọng (viết theo nguồn, dạng hiển thị hiện tại của engine)

```c
// func sumSquares(n int32) int32 — vòng lặp đếm + tích lũy
int __usercall main.sumSquares(int a1@<r0>)   // a1 = SP; n đọc từ ô tham số
{
  v_saved = SP;
  v_i = 1; v_total = 0;
  while ( v_i <= n )          // n = *(int*)(SP + 6) — ô stack tham số
  {
    v_total += v_i * v_i;
    ++v_i;
  }
  *(int*)(SP + 16) = v_total; // ô kết quả (x=f chưa áp dụng — TODO engine)
  SP = v_saved + 8;
  return 0;                   // trả SP (PC_B convention)
}
```

Ghi chú kỳ vọng: (1) dispatcher `switch(a1)` KHÔNG xuất hiện — vòng là
`while` thật (G3 mọi kích thước / G11); (2) guard morestack KHÔNG xuất
hiện (G10); (3) tham số `n` và kết quả vẫn đi qua ô stack Go — G5/G4 còn
mở, đây là dạng trung thực hiện tại; (4) main nở println thành
printlock/printint/printnl/printunlock, 3 lần, đúng thứ tự.

## Thực tế (dump 14/11, deploy rev32 `01cf84b6…`)

`main.sumSquares`: while `v2 <= *(int*)&byte_2[SP+6]`, thân `v3 += v2*v2;
++v2;`, kết quả `*(SP+16) = v3`, epilogue `SP = v1+8; return 0`.
`main.main`: đúng 3 cặp (gọi sumSquares → printint) theo thứ tự nguồn.

## Bảng PASS/FAIL

| Kỳ vọng | Kết quả | Phân loại |
|---|---|---|
| Vòng `while` thật, đúng điều kiện `i <= n` | ✅ PASS | — |
| Thân `total += i*i` + `++i` | ✅ PASS | — |
| 0 `switch (a1)` (dispatcher replaced) | ✅ PASS | — |
| 0 guard morestack | ✅ PASS | — |
| Thứ tự 3 lần println trong main | ✅ PASS | — |
| `n` hiện là tham số C (thay vì load ô stack) | ❌ FAIL | **engine limit đã biết** (G5 chữ ký — còn mở, không phải bug mới) |
| Kết quả hiện `return v_total` / `x = f(...)` | ❌ FAIL | **declined engine** (x=f apply pending — INTERR 52849, đã ghi T0 rev31) |

Kết luận: **PASS cấu trúc hoàn toàn**; 2 điểm trễ đều là giới hạn engine
đã biết + đã decline, không có bug mới.
