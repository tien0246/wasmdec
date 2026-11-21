# G12 "map" — pseudocode kỳ vọng vs dump thực tế

Nguồn: `tests/go/map/main.go` (go1.27.4). Dump: `main.bump.c` +
`main.main.c` (module-wide 1250/1250 OK, 0 INTERR).

## Kỳ vọng (dạng hiển thị hiện tại)

```c
// func bump(m map[string]int32, k string) int32
int __usercall main.bump(int a1@<r0>)
{
  SP -= 48;                                  // khung gọi runtime
  *(u64*)SP = <map type word>;               // type word của map[string]int32
  *(u64*)(SP+8)  = *(u64*)(SP+54);           // m (hash map header)
  *(u64*)(SP+16) = *(u64*)(SP+62);           // k {ptr,len}
  runtime.mapaccess1_faststr(SP);            // m[k] -> con trỏ giá trị
  v_old = *(i32*)*(u64*)(SP+30);             // đọc giá trị cũ

  <đẩy lại type word + m + k>
  runtime.mapassign_faststr(SP);             // m[k] = -> con trỏ ô ghi
  v_dst = *(u64*)(SP+30);
  if ( v_dst == 0 ) runtime.sigpanic();      // nil map check (hiện thị hiện tại)
  *(i32*)v_dst = v_old + 1;

  <đẩy lại type word + m + k>
  runtime.mapaccess1_faststr(SP);            // return m[k]
  *(i32*)(SP+80) = *(i32*)*(u64*)(SP+30);    // ô kết quả (TODO engine)
  SP += 56;
  return 0;
}
```

Ghi chú kỳ vọng: đúng CẢNH BẢN runtime của Go 1.27 cho khóa string — cặp
`mapaccess1_faststr` / `mapassign_faststr`; mỗi lần gọi đẩy type word +
map header + khóa {ptr,len} qua cửa sổ stack (G4 chưa giá trị hóa); nil
check mapassign hiện `runtime.sigpanic` (D5 default-chưa-ẩn — trung thực).

## Thực tế

Đúng chuỗi 3 lời gọi (access → assign → access), đúng thứ tự với source;
`*(i32*)v3 = *(i32*)(...) + 1` ghi giá trị mới qua con trỏ của
mapassign; type word 851552 đẩy trước mỗi lời gọi; nil check + sigpanic
đúng vị trí; kết quả ghi SP+80.

## Bảng PASS/FAIL

| Kỳ vọng | Kết quả | Phân loại |
|---|---|---|
| Cặp `mapaccess1_faststr` + `mapassign_faststr` | ✅ PASS | — |
| 3 lời gọi đúng thứ tự nguồn (đọc, ghi, đọc-trả) | ✅ PASS | — |
| Ghi `+1` qua con trỏ kết quả mapassign | ✅ PASS | — |
| Type word map + khóa {ptr,len} đẩy trước mỗi gọi | ✅ PASS | — |
| 0 dispatcher / 0 morestack guard | ✅ PASS | — |
| `m`, `k` hiện là tham số C theo giá trị | ❌ FAIL | engine limit đã biết (G4/G5 — còn mở) |
| nil check ẩn kèm counter (D5) | ❌ FAIL | kỳ vọng dạng hiện tại — D5 tùy chọn chưa mặc định |

Kết luận: **PASS cấu trúc hoàn toàn**; không có bug mới.
