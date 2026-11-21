# G12 "slice" — pseudocode kỳ vọng vs dump thực tế

Nguồn: `tests/go/slice/main.go` (go1.27.4). Dump: `main.sumSlice.c` +
`main.main.c` (module-wide 1250/1250 OK, 0 INTERR).

## Kỳ vọng (dạng hiển thị hiện tại)

```c
// func sumSlice(xs []int32) int32 — duyệt theo len + index
int __usercall main.sumSlice(int a1@<r0>)
{
  v_saved = SP;
  v_i = 0; v_total = 0;
  while ( v_i < *(i64*)(SP + 14) )   // len(xs) — ô tham số +8 trong header
  {
    v_total += *(int*)( *(i64*)(SP + 6) + 4 * v_i++ );  // xs[i]: data + 4*i
  }
  *(int*)(SP + 32) = v_total;        // ô kết quả (TODO engine: x=f)
  SP = v_saved + 8;
  return 0;
}
// main: xs := []int32{10,20,30,40}; sumSlice(xs);
//       append(xs, 50) -> runtime.growslice; sumSlice; len/cap in mệnh đề println
```

Ghi chú kỳ vọng: slice header {ptr@SP+6, len@SP+14, cap@SP+22} chưa gộp
thành 1 giá trị (G4/G5); phép nhân index `4*i` phải hiện trong địa chỉ
phần tử; `append` phải sinh lời gọi `runtime.growslice` trong main.

## Thực tế

`while (v2 < *(u64*)&byte_2[SP+14])`; phần tử
`v3 += *(_DWORD *)((u32)*(u64*)&byte_2[SP+6] + 4 * (_DWORD)v2++)` — load
data pointer + scale 4*i + i++ fuse đúng một biểu thức. main có
`runtime.growslice`, `len`/`cap` (printsp cho đa đối số println).

## Bảng PASS/FAIL

| Kỳ vọng | Kết quả | Phân loại |
|---|---|---|
| Vòng duyệt theo `len(xs)` (load 8 byte len) | ✅ PASS | — |
| Phần tử `data + 4*i` trong 1 biểu thức, i++ fuse | ✅ PASS | — |
| 0 dispatcher / 0 morestack guard | ✅ PASS | — |
| `append` → `runtime.growslice` trong main | ✅ PASS | — |
| Slice header hiện thành 1 giá trị slice (C) | ❌ FAIL | engine limit đã biết (G4 theo giá trị — còn mở) |
| Kết quả `return`/`x=f` | ❌ FAIL | declined engine (x=f pending) |

Kết luận: **PASS cấu trúc hoàn toàn** — đúng ngữ nghĩa nguồn ở tầng tải/
lưu; giới hạn còn lại là hiển thị giá trị hóa (đã biết, đã decline).
