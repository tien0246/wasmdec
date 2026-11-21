# T0 freeze — golden corpus + corpus-sample hashes (anchor cho M2 file moves)

Todo §9.3 T0: trước khi dời file (T1/T2), đóng băng (1) golden corpus
trong git và (2) các sample hash tái tạo được trên binary deployed. Mỗi
move commit phải tái tạo ĐÚNG các sha dưới đây; sai khác = regress —
dừng ngay và báo Root trước khi tiếp tục.

## Binary & tree khi freeze

| Mục | Giá trị |
|---|---|
| HEAD | `c1aa4a9` (golden regen) ← `1ea5e2a` (H1) ← `c8f0db8` (P0-8) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `6498973e96dfc55715fb2858d268c160` |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `1546ad8e32bcd37dc55198c97b144ed0` |
| Corpus golden (git) | commit `c1aa4a9`: 694 file +1 dòng `// wasmhex: bounds=hidden`, 130 identical (validator-refuse), 88 mới, 3 orphan xóa, 1 ngoại lệ float_ops (pre-existing, bằng chứng trong commit message) |

## 1. duckdb.wasm — sample 634 hàm (stride 100) × 2 mode

Thủ tục tái tạo (mỗi mode):

```sh
[WASMHEX_BOUNDS=explicit hoặc bỏ biến] \
WASMHEX_STRIDE=100 idat -c -A -pwasmhex -T"WebAssembly (wasmhex)" \
  -o/tmp/duck_<mode>.i64 -L/tmp/duck_<mode>.log \
  -Stests/integration/ida/verify_wasmhex_duckdb_all_f5.py \
  wasm-test/wasm-test/duckdb.wasm
shasum -a 256 /tmp/duck_<mode>.log
```

Binary = modules trong `~/.idapro` (không IDAUSR override).

| Mode | Kết quả | sha256 log đầy đủ |
|---|---|---|
| hidden | `WASMHEX_F5_DONE total=634 failures=0` | `527f2d5d8e23cb0daa7a8b8aa4572730143b03fe0a2e8785afd23f59e4a3805d` |
| explicit | `WASMHEX_F5_DONE total=634 failures=0` | `01f499f4f5d98413065de47de976da461a007814f5707d430702241a8b57498a` |

## 2. main.wasm — 100 hàm đầu theo Functions() order

Danh sách 100 hàm của M1-S2 không tái tạo exact được (tempdir đã dọn);
theo phương án Root: 100 hàm ĐẦU theo `idautils.Functions()` (thứ tự EA).

```sh
T0_MODE=first100 T0_COUNT=100 idat … -S<t0_sweep.py> wasm-test/wasm-test/main.wasm
shasum -a 256 main100.log
```

Kết quả: `T0_SWEEP total=100`, **ok=82 / fail=18** (các hàm fail-closed
hiện hữu — MERR_COMPLEX v.v.; snapshot đúng bản chất freeze).

sha256 log đầy đủ (rev1): `0428da009dff6b835e74c8965f6f388ab14e1d9ab30737be0e04c4879e23c23b`

### T0 rev2 — cập nhật main100 sau M3c (`4ea2021`)

| Mục | Giá trị |
|---|---|
| HEAD | `4ea2021` (M3c: Go scans run on the one decoder — memarg≥128 fixed) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `576dc30ef190ec683f880d679be3d60f` |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `8068169b211516e34deeea95f87a0af0` |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2)** |
| main100 `sha256` log đầy đủ (rev2) | `ce2fbc161e545c6d741e734b172faff034da4346103478968476b00f0ca8ff15` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `f4cf9497cf4c467891ca7a00b9c4e49f7072998f04ea30da4019a32411680804` (rev1: `9eb2d2732bb598ebb82b0e2688ce090145eb8faf169691ccadf219e3f07fbe16`) |
| Số hàm đổi / total | **1 / 100** (`wasm_func_27` / `body[7]` tại `LABEL_22`; **99/100 hàm còn lại byte-identical**) |

**Cơ chế & dẫn chứng (`4ea2021`):**
- M3c chuyển các bộ quét Go sang dùng chung một decoder (`core/binary/instruction_stream`), sửa lỗi giả định memarg 1 byte khi `memarg.offset >= 128` (LEB128 2 byte).
- Tại `wasm_func_27` (`body[7]`, `LABEL_22`), backward-walk thu thập đối số gọi hàm giờ nhìn thấy lệnh store ở `off=136` (`37 03 88 01` — thực chất là caller frame spill ở `v10 + 112`, tức `0x18 + 112 = 136`) và gom thành đối số thứ 4:
  ```diff
  -  *(_QWORD *)&byte_100000018[v10 + 112] = v8;
  -  ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>, __int64@<r6:r7>))wasm_func_1310)(109408, v8, v8);
  +  ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>, __int64@<r6:r7>, __int64))wasm_func_1310)(v8, 109408, v8, v8);
  ```
- Việc giới hạn vùng gom đối số theo đúng outgoing-args region (`GoCallIR` / G4) thuộc phạm vi M3d — khi M3d land, call site này sẽ đổi lại hợp lệ một lần nữa.

### T0 rev3 — cập nhật main100 và gowasm-balls sau G3 (`9f8626c`)

| Mục | Giá trị |
|---|---|
| HEAD | `9f8626c` (G3: replace Go resume dispatcher with direct control flow and remove size gate) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `a6365eeb02d5bb0cc2668fb8b24fb821` |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `e3f2f330bce0ac267986f79fa45cd878` |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3)** |
| main100 kết quả | `T0_SWEEP_DONE ok=83 fail=17` (tăng +1 hàm `OK`: `wasm_func_32`; **0 hàm còn `switch (a1)`**) |
| main100 `sha256` log đầy đủ (rev3) | `f7e4c0b19953e061ce64e51aea5569569786b71121c1950b82741ecc42d33953` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `66dcf67d323ae5a3ceecce3b05aa43336a09e7e834f59fe8ac2d28233d5b57e6` |
| main100 số hàm đổi / total | **74 / 100** đổi hợp lệ (`68` `switch-removed` trong đó `32` kèm `goto-folded`; `5` `2-arm-dispatch-removed`; `1` `new-decompile-success`); **26 / 100** giữ nguyên (`9` `OK` không có dispatcher + `17` `FAIL -22`) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev3) | `49797f792f3312c475722ddd25effce439391e204c53dec2825aa208a9a41c6b` |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev3) | `6cf5fc9161997d70d1890a841b04867fc16bc09bb99748cc5d86ed093f8da3f1` (trước G3: `ad800c02d2668c8c7bb15aeebe9a34fb2ac8da1634008e10dae6ccbf34af193b`) |
| gowasm-balls số hàm đổi / total | **4 / 5** đổi hợp lệ (`main.main`, `main.loop`, `main.__Ball_.draw`, `main.main.func1`); **1 / 5** (`go_buildid`) byte-identical |

**Bảng phân loại thay đổi (rev3 — commit `9f8626c`):**

| Corpus / tập mẫu | Tổng hàm | Не đổi (`OK` / `FAIL`) | `switch-removed` (`switch (a1)` → 0) | Kèm `goto-folded` (`goto LABEL_` giảm) | `2-arm-dispatch-removed` (`if (a1 != 0) trap` → 0) | `new-decompile-success` (`FAIL -22` → `OK`) | Tổng dòng (`OK` chung) |
|---|---:|---:|---:|---:|---:|---:|---:|
| `main.wasm` (`first100`) | 100 | 26 (9 `OK`, 17 `FAIL`) | 68 (`68 → 0`) | 32 (`504 → 48` `goto`) | 5 (`wasm_func_20,22,34,78,79`) | 1 (`wasm_func_32`) | `5.865 → 3.438` (`-2.427`, `-41,4%`) |
| `gowasm-balls` (5-hàm smoke) | 5 | 1 (`go_buildid`) | 4 (`4 → 0`) | 2 (`51 → 0` `goto`) | 0 | 0 | `754 → 524` (`-230`, `-30,5%`) |
| `tests/golden/` (13 Go fixtures) | 55 | 51 (`51 PASS`) | 4 (`s2`, `s7`, `s11`, `p05`) | 0 | 0 | 0 | `93 → 25` trên 4 hàm (`-68` dòng) |
| `duckdb.wasm` (`stride=100`) | 634 | 634 (`634 OK`) | 0 | 0 | 0 | 0 | Bất biến 100% (`5f728cb0…`) |

**Cơ chế & dẫn chứng cho từng lớp thay đổi (`9f8626c`):**
1. **Lớp `switch-removed` + `goto-folded` (`68` hàm ở `main100`, `4` hàm ở `gowasm-balls`, `4` hàm ở `tests/golden/`):**
   - `emit_go_dispatcher_replacement` (`decompiler/lower_control.cpp`) tiêu thụ `local.get 0` và thay `br_table` đầu hàm (`go_dispatch_ea`) bằng một lệnh `m_goto` trực tiếp tới nhánh `PC_B == 0` (`go_dispatch_targets[0]`), đồng thời xóa size gate (`max_records=64` / `max_table=512`). Các nhánh `PC_B != 0` chỉ phục vụ resume goroutine không còn cạnh đi tới từ đầu hàm nên bị DCE cắt bỏ; các cạnh `emit_go_direct_jmp` nối trực tiếp giữa các khối `pc == k` giúp bộ cấu trúc hóa của Hex-Rays gộp toàn bộ vòng lặp/nhánh mà trước đây bị khóa trong `switch (a1)` + `goto LABEL_`.
   - Dẫn chứng cụ thể trên `main.loop` (`gowasm-balls`, `0x18e52c`, 148 → 90 dòng, `switch (a1)` `1 → 0`, `goto LABEL_` `13 → 0`):
     ```diff
     -  while ( 2 )
     -  {
     -    switch ( a1 )
     -    {
     -      case 0:
     -        if ( (unsigned int)v2 <= *(_DWORD *)(*(_QWORD *)&wasmhex_global_2 + 16LL) )
     -        ...
     -      case 5:
     -LABEL_9:
     -        if ( v13 >= v12 )
     -          goto LABEL_11;
     +  for ( i = 0; i < v9; i = v16 + 1 )
     +  {
     +    v15 = i;
     +    if ( (unsigned __int64)i >= *(_QWORD *)&byte_100000000[(unsigned int)unk_100150B98 + 8] )
     +      runtime.panicIndex();
     ```
2. **Lớp `2-arm-dispatch-removed` (`5` hàm leaf ở `main100`: `wasm_func_20`, `wasm_func_22`, `wasm_func_34`, `wasm_func_78`, `wasm_func_79`):**
   - Các hàm leaf Go không có lời gọi con có bảng `br_table [B0, default]` gồm đúng 2 nhánh (`targets.size() == 2`, `jmps.empty()`). Trước G3, `collect_go_jmps` bỏ qua hàm có `jmps.empty()` nên `br_table` 2 nhánh được Hex-Rays rút gọn thành `if ( a1 != 0 ) __builtin_trap();`. Sau G3, dispatcher 2 nhánh được thay bằng cạnh trực tiếp tới `B0` (`PC_B == 0`), loại bỏ hoàn toàn kiểm tra `if ( a1 != 0 ) __builtin_trap();` (giảm 2 dòng/hàm).
3. **Lớp `new-decompile-success` (`1` hàm ở `main100`: `wasm_func_32` @ `0x168d`, `body[12]`, 244 byte):**
   - `wasm_func_32` có `resume_table` 7 nhánh (`targets=[38, 129, 198, 224, 225, 232, 36]`), 1 const-JMP (`@192 case=3 -> @224`) và 1 `call_indirect` qua `PC_F`. Trước G3, CFG phẳng của `switch (a1)` lồng với chuỗi `call_indirect` vượt ngưỡng độ phức tạp của Hex-Rays (`T0_FAIL wasm_func_32 code=-22` / `MERR_COMPLEX`). Sau khi G3 loại bỏ `switch (a1)` và nối thẳng cạnh JMP `@192 -> @224`, `wasm_func_32` decompile thành công (`OK`, 0 `switch (a1)`).

### T0 rev4 — cập nhật main100 sau G4 (`b510219`)

| Mục | Giá trị |
|---|---|
| HEAD | `b510219` (G4: lower Go indirect calls through PC_F as single direct/indirect calls) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `ecf71d2a973488934d0c2d0729d4abeb` |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `a95af612ed5d34ab923af9e2831f24d4` |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3→rev4)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (giải phóng toàn bộ **+17 / 17** hàm `FAIL -22` còn lại; **0 hàm còn `switch (a1)`**) |
| main100 `sha256` log đầy đủ (rev4) | `71d27696beaf0c2b475a7dca4ac7354317c10b9dd795511970a4d70c9b0c5c2f` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `7e2dbbe8e45dd795eccf8193611700e7895f3a136b673b382b4c05e4bb207754` (rev3: `66dcf67d323ae5a3ceecce3b05aa43336a09e7e834f59fe8ac2d28233d5b57e6`) |
| main100 số hàm đổi / total | **18 / 100** đổi hợp lệ (`1` `cascade-removed` trên hàm đã `OK` là `wasm_func_32`: `9.765 → 21` dòng; `17` `new-decompile-success` từ `FAIL -22` → `OK` đồng thời `cascade-removed`); **82 / 100** hàm `OK` còn lại **byte-identical 100%** |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev4) | `49797f792f3312c475722ddd25effce439391e204c53dec2825aa208a9a41c6b` (BẤT BIẾN so với rev3) |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev4) | `6cf5fc9161997d70d1890a841b04867fc16bc09bb99748cc5d86ed093f8da3f1` (BẤT BIẾN 5/5 hàm byte-identical so với rev3) |

**Bảng phân loại thay đổi (rev4 — commit `b510219`):**

| Corpus / tập mẫu | Tổng hàm / site | Không đổi (`OK` byte-identical) | `cascade-removed` (O(N) 6.510塊 → O(1) `m_icall`) | `new-decompile-success` (`FAIL -22` → `OK`) | `direct-call-proven` (constant `PC_F` → `m_call`) | Tổng dòng (`OK` chung / toàn bộ) |
|---|---:|---:|---:|---:|---:|---:|
| `main.wasm` (`first100`) | 100 hàm | **82** (`82 OK`, `0 FAIL`) | **18** hàm (`21` site `PC_F`) | **17** (`wasm_func_91..93,95..97,103,107,109,110,112,113,115..119`) | 0 (trong `first100`) | 83 hàm `OK` chung: `13.203 → 3.459` (**−9.744**, **−73,8%**); cả 100 hàm `OK`: `6.505` dòng |
| `main.wasm` (toàn module) | 3.494 site `PC_F` | — | **3.494** site (`3.488` `m_icall` + `6` `m_call`) | Giải phóng toàn bộ các hàm `PC_F` lớn (`wasm_func_3011`, `3270`, ...) | **6** site (`wasm_func_332` ×2, `725`, `803` ×2, `2815`) | `wasm_func_3002`: `21.820 → 1.345` (**−93,8%**); `3011`: `FAIL → 2.067`; `3270`: `>600s → 2.573` |
| `gowasm-balls` (toàn module & 5-hàm smoke) | 5 hàm smoke (183 site `PC_F` toàn module) | **5 / 5** smoke byte-identical | **183** site toàn module (`177` `m_icall` + `6` `m_call`) | 0 (smoke 5/5 đã `OK`) | **6** site (`runtime.startCheckmarks` ×2, `runtime.main`, `runtime.injectglist` ×2, `internal_poll.__FD_.Write`) | Smoke giữ nguyên `524` dòng (`6cf5fc91…`) |
| `tests/golden/` (12 Go fixtures) | 52 hàm | **52 / 52** (`52 PASS`) | 0 (fixtures Go hiện hữu chỉ dùng direct `call`) | 0 | 0 | Bất biến 100% (`52/52 PASS`, `0` repair) |
| `duckdb.wasm` (`stride=100`) | 634 hàm | **634 / 634** (`634 OK`) | 0 | 0 | 0 | Bất biến 100% (`5f728cb0…`) |

**Cơ chế & dẫn chứng cho từng lớp thay đổi (`b510219`):**
1. **Lớp `cascade-removed` (`18` hàm / `21` site ở `main100`, `3.494` site toàn `main.wasm`, `183` site toàn `gowasm-balls`):**
   - Trong Go wasm (`cmd/internal/obj/wasm`, `pc_f_shift == 16`), mỗi lời gọi gián tiếp đẩy địa chỉ hàm 64-bit `funcval = *(uint64*)fn_ptr` lên stack, lưu `SP -= 8; *(i64*)SP = (PC_F<<16)|pc_after`, rồi trích chỉ số bảng hàm bằng đúng 5 lệnh `i64.const 16; i64.shr_u; i32.wrap_i64; local.set 0; [i32.const 0]; local.get 0; call_indirect 0 0`. Trước G4, `call_indirect` đi vào `emit_sparse_indirect_call` và dựng chuỗi 3.255 nhánh `m_jz` + 3.255 khối gọi (6.510 khối/site).
   - Với G4, `build_go_function_irs` (`core/toolchains/go/ir.cpp`) nhận diện mẫu `PC_F` chuẩn, gom các store đối số trước biểu thức nạp `funcval` + `global.set 1` (`CTXT`), và ghi `is_pc_f_indirect` + `pc_f_extract_offsets[5]` vào `go_call_ir_t` (blob `'x'` v3 tail `GOPC`). `wasmhex_filter_t::apply` tiêu thụ 5 lệnh trích `PC_F` như cặp trung hòa stack để giữ nguyên toán hạng `funcval` 8 byte trên đỉnh stack trừu tượng, còn `emit_call` phát ra **đúng 1 lệnh `m_icall`** (0 khối phụ).
   - Dẫn chứng cụ thể trên `wasm_func_32` (`0x168d`, call site `0x175a` / `@205`, từ **9.765 dòng** với `switch` 3.255 nhánh `case 0x1000u..0x1CB6u` xuống **21 dòng**, 1 lệnh gọi hàm gián tiếp):
     ```diff
     -  v6 = *(_QWORD *)&dword_100211530 >> 16;
     -  switch ( v6 )
     -  {
     -    case 0x1000u:
     -      v4 = wasm_func_20(0);
     -      goto LABEL_3260;
     -    ... [3.253 nhánh case 0x1001u .. 0x1CB5u] ...
     -    case 0x1CB6u:
     -      v4 = wasm_func_3274(0);
     -LABEL_3260:
     -      if ( v4 != 0 )
     -        return 1;
     -      break;
     -    default:
     -      if ( v6 >= 0x1CB7 )
     -        wasmhex_trap_call_indirect_bounds();
     -      wasmhex_trap_call_indirect_null();
     -  }
     +  if ( *(_QWORD *)&dword_100211530 != 0 )
     +  {
     +    wasmhex_global_1 = *(_QWORD *)&dword_100211530;
     +    (*(void (__usercall **)())&byte_100000000[dword_100211530])();
     +  }
     ```
2. **Lớp `new-decompile-success` (`17 / 17` hàm `FAIL -22` ở `main100` + `wasm_func_3011` + `wasm_func_3270`):**
   - Toàn bộ 17 hàm thất bại với `MERR_COMPLEX` (`code=-22`) trong `main100` sau G3 (`wasm_func_91, 92, 93, 95, 96, 97, 103, 107, 109, 110, 112, 113, 115, 116, 117, 118, 119`) và các hàm lớn như `wasm_func_3011` (8 site `PC_F`, trước G4: `qty=52.907` ở `preopt` → `FAIL -22` sau 421,8 s) đều vượt trần khối của Hex-Rays chỉ vì chuỗi 6.510 khối/site. Sau G4, `wasm_func_3011` giảm từ `52.907` khối xuống **`795` khối** ở `preopt` (`307` khối ở `glbopt`) và decompile thành công (`OK`, 2.067 dòng) trong **0,55 s** (`glbopt` 332,9 ms); `main100` đạt **100 / 100 `OK`** (`p50 = 4,3 ms`, `p95 = 77,8 ms`, `max = 268,2 ms`).
3. **Lớp `direct-call-proven` (`6` site ở `main.wasm`, `6` site ở `gowasm-balls`):**
   - Khi biểu thức đẩy `funcval` 64-bit trước `SP -= 8` là hằng tĩnh chứng minh được (`i64.const V` hoặc `i64.const addr; i32.wrap_i64; i64.load align=3 off` đọc từ descriptor closure tĩnh trong vùng dữ liệu chỉ-đọc `rodata` với `V & 0xFFFF == 0`), `try_resolve_constant_pc_f_callee` phân giải `slot = V >> 16` qua bảng `table_slots` về `fn_index` trực tiếp (`call_ir.fn_index = proven_fn`), và `emit_call` phát lệnh gọi trực tiếp `m_call` kèm đúng cửa sổ đối số của callee.
   - Dẫn chứng cụ thể:
     - Trên `main.wasm`: `wasm_func_725` @ `0x9b428` (`call_indirect` tại offset `@2072`, đọc `funcval = 481492992 = 7347 << 16` tại `0x4d290` → slot `7347` → `wasm_func_3271`):
       ```c
       wasmhex_global_1 = 316048;
       ((void (__usercall *)())wasm_func_3271)();
       ```
     - Trên `wasm_func_2815` @ `0x35812b` (`call_indirect` tại offset `@1463`, đọc `funcval = 443482112 = 6767 << 16` tại `0x4dc28` → slot `6767` → `wasm_func_2691` với 4 đối số):
       ```c
       wasmhex_global_1 = 318504;
       ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>, __int64@<r6:r7>, __int64))wasm_func_2691)(v15, v17, v16, v11);
       ```

### T0 rev5 — khôi phục 11 hàm Go lỗi `WASMHEX_TRIM surplus=1` (sau G9) + M4-S1 prototypes/type-cache/ICF metadata

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `87e616e89d4de629002ab56306a2d84d` (`WASMHEX_BUILD_MARKER_TRIM_SURPLUS1_RECOVERY`) · sau M4-S1: `544612d5aa2c2b65bf88f900f0b1346f` (`WASMHEX_BUILD_MARKER_M4_S1_HONEST_PROTOTYPES_TYPE_CACHE_ICF`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `bb6327ce2717d4a99fba72b5ce202896` (`WASMHEX_BUILD_MARKER_TRIM_SURPLUS1_RECOVERY`) · sau M4-S1: `c87fb81aa0ab0fa458bfbea21c1fe97a` (`WASMHEX_BUILD_MARKER_M4_S1_HONEST_PROTOTYPES_TYPE_CACHE_ICF`) |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3→rev4→rev5 + M4-S1)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**100 / 100 hàm byte-identical 100%** so với rev4) |
| main100 `sha256` log đầy đủ (rev5) | `c2f9c67dec0adc4c95df30b9f5e42c1ce11c984f758ad06d088dea77c27c2e02` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `7e2dbbe8e45dd795eccf8193611700e7895f3a136b673b382b4c05e4bb207754` (**BẤT BIẾN 100/100 hàm byte-identical so với rev4 & sau M4-S1**) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev5) | `49797f792f3312c475722ddd25effce439391e204c53dec2825aa208a9a41c6b` (**BẤT BIẾN so với rev3 & rev4**) |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev5) | `6cf5fc9161997d70d1890a841b04867fc16bc09bb99748cc5d86ed093f8da3f1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev3, rev4 & sau M4-S1**) |
| **11 hàm Go được khôi phục (`FAIL -31` → `OK`)** | **`7 / 7` trên `main.wasm`** (`wasm_func_361`, `1301`, `1547`, `2422`, `2425`, `2838`, `3117` — pseudocode-only sha256 `af47dc1e0526baf326699bc1466f8a860f23a1a5015db93981714f275644d5ab`, full log `a4fcc5ad1c7986c81af914276a00ceaaffe703911f18fc2f16b0011c3481370b`) + **`4 / 4` trên `gowasm-balls`** (`runtime.initMetrics`, `reflect.addReflectOff`, `sync.__poolDequeue_.popTail`, `sync.OnceFunc.func1.1.1` — pseudocode-only sha256 `c2b833691e2957f77ab7164a53cf09bddc546310729655de10e93176053f2517`, full log `a8d07badf2e5378c9edf53105b9dd6bbb28128bf94e426832c28ce60ccac4653`) |
| **M4-S1 C++ prototype & type-cache depollution (`duckdb.wasm`)** | F5-subset `5f728cb0…` bất biến (`634/634 OK` cả `hidden` và `explicit`); thay đổi văn bản pseudocode trên `duckdb.wasm` có chủ đích do (1) `scan_wasm_function_type_cache` chỉ seed các kiểu scalar wasm thuần túy (loại bỏ hoàn toàn các ép kiểu giả `(struct std____2__shared_ptr *)` / `(struct duckdb__web__QueryConfig *)` tại mọi call site cùng arity, kể cả 3 dòng trong `tests/integration/golden/mergeProcessed_baseline.txt`), và (2) `make_mangled_prototype` ánh xạ theo từng tham số (`optional_ptr<T> -> struct T *`, enum/by-value `-> int`, sret ẩn `void *__ret`) |

**Bảng phân loại thay đổi (rev5):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | `new-decompile-success` (`FAIL -31 WASMHEX_TRIM` → `OK`) | `switch (a1)` | Ghi chú |
|---|---:|---:|---:|---:|---|
| `main.wasm` (`first100`) | 100 | **100 / 100** (`100 OK`) | 0 (trong `first100`) | 0 | Bất biến 100% (`7e2dbbe8…`, `6.505` dòng) |
| `main.wasm` (7 hàm `WASMHEX_TRIM` toàn module) | 7 | — | **7 / 7** (`wasm_func_361`: 2.791 dòng, `1301`: 109 dòng, `1547`: 79 dòng, `2422`: 20.123 dòng, `2425`: 60 dòng, `2838`: 617 dòng, `3117`: 60 dòng) | 0 | Nâng tổng `main.wasm` lên **3.253 / 3.255 `OK` (99,94%)**, **0** `MERR_BADARCH (-31)` |
| `gowasm-balls` (5 hàm smoke) | 5 | **5 / 5** (`5 OK`) | 0 | 0 | Bất biến 100% (`6cf5fc91…`, `524` dòng) |
| `gowasm-balls` (4 hàm `WASMHEX_TRIM` toàn module) | 4 | — | **4 / 4** (`runtime.initMetrics`: 3.034 dòng, `reflect.addReflectOff`: 109 dòng, `sync.__poolDequeue_.popTail`: 81 dòng, `sync.OnceFunc.func1.1.1`: 61 dòng) | 0 | Nâng tổng `gowasm-balls` lên **1.757 / 1.759 `OK` (99,89%)**, **0** `MERR_BADARCH (-31)` |
| `duckdb.wasm` (`stride=100`) | 634 | **634 / 634** (`634 OK`) | 0 | 0 | Bất biến 100% (`5f728cb0…`) |

**Cơ chế & dẫn chứng cho lớp `new-decompile-success` (`11 / 11` hàm `WASMHEX_TRIM surplus=1`):**
1. **Nguyên nhân gốc theo đặc tả WebAssembly:**
   - Trong Go wasm (`go1.26` / `go1.27`), mỗi phép gán con trỏ vào biến toàn cục hoặc trường struct trên heap được bọc bởi một khối write barrier có `results = 0`:
     ```wasm
     ;; Nếu writeBarrier.enabled == 0, nhánh nhanh nhảy thẳng tới `end $wb_done`
     if (result)
       i32.const <k_wb_done>
       local.set 0
       br $wb_done
     end
     ;; Nhánh chậm: gọi runtime.gcWriteBarrier{1,2}, sau đó nạp lại thanh ghi R từ frame [SP + slot]
     ...
     local.get 1
     i64.load offset=<frame_slot>
     local.set <R>
     end $wb_done
     ;; Ngay sau `end $wb_done` (results=0, stack trống): thực hiện phép gán con trỏ thật sự
     <dst_addr>          ;; Sub-shape A: `i64.const addr; i32.wrap_i64` | Sub-shape B: `local.get ptr; i32.wrap_i64`
     <val_expr>          ;; `local.get R` hoặc `i64.const 0`
     i64.store offset=0  ;; *(uint64*)dst_addr = val_expr
     ;; Tiếp theo là các store đối số ra [SP + off] và lệnh gọi hàm Go
     ```
   - Khi một lời gọi hàm Go xuất hiện ngay sau phép gán con trỏ hậu write-barrier này, vòng quét ngược tìm `local.get 1` (`SP`) trong `build_go_function_irs()` (`native/wasmhex/core/toolchains/go/ir.cpp`) đã đi xuyên qua ranh giới `end $wb_done` (`0x0B`) vào bên trong khối write barrier và bắt nhầm lệnh `local.get 1` của `local.get 1; i64.load <frame_slot>; local.set <R>` làm đáy địa chỉ `SP` cho lệnh `i64.store offset=0` hậu barrier.
   - Hệ quả: lệnh ghi bộ nhớ `*(uint64*)dst_addr = val_expr` bị đánh dấu nhầm vào `go_call_arg_store_types`. Khi G3 nối thẳng nhánh nhanh `if (!writeBarrier.enabled) br $wb_done` tới `end $wb_done`, khối `wb_done` kết thúc với stack rỗng (`0` toán hạng), sau đó `<dst_addr>` đẩy 1 địa chỉ `i32` và `<val_expr>` đẩy 1 giá trị `i64`, rồi `go_call_arg_store_types` pop 2 toán hạng nhưng đẩy trả lại `val_expr` lên stack để truyền làm đối số hàm — bỏ sót 1 toán hạng dư (`surplus=1`) trên stack trừu tượng và kích hoạt cổng fail-closed `WASMHEX_TRIM` tại `end` tiếp theo (hoặc ngay tại `end` của block bao ngoài).
2. **Sửa lỗi trong `build_go_function_irs()` (`native/wasmhex/core/toolchains/go/ir.cpp`):**
   - Dừng vòng quét ngược tìm `local.get 1` ngay khi gặp bất kỳ lệnh biên điều khiển có cấu trúc nào (`0x02` `block`, `0x03` `loop`, `0x04` `if`, `0x05` `else`, `0x0B` `end`, `0x0C` `br`, `0x0D` `br_if`, `0x0E` `br_table`, `0x0F` `return`). Phép gán con trỏ hậu write-barrier không có `local.get 1` trong cùng khối cơ bản nên không bị gom nhầm vào `call_ir.args`, được hạ thành lệnh ghi bộ nhớ thật sự (`*(_QWORD *)dst = val`), giữ độ sâu stack trừu tượng bằng `0` tại mọi nhãn `end` (`surplus=0`) và bảo toàn đúng danh sách đối số của lời gọi hàm kế tiếp.
   - Dẫn chứng cụ thể trên `sync.OnceFunc.func1.1.1` (`gowasm-balls` @ `0x142ba0`, tương ứng `wasm_func_2425` @ `0x2d840e` trên `main.wasm`, từ `FAIL code=-31` tại `0x142c56` → `OK` 61 dòng):
     ```c
     v4 = *(_QWORD *)((char *)&unk_100000008 + (unsigned int)wasmhex_global_1);
     *(_QWORD *)((char *)&unk_100000028 + v2) = v4;
     if ( !v3 )
     {
       *(_QWORD *)&byte_100000014[v2 + 4] = *(_QWORD *)((char *)&unk_100000000 + (unsigned int)v4);
       __SET_PAIR__(v5, v6, runtime.gcWriteBarrier1());
       v7 = v6;
       v2 = wasmhex_global_0;
       *(_QWORD *)((char *)&unk_100000000 + v7) = *(_QWORD *)&byte_100000014[wasmhex_global_0 + 4];
       v4 = *(_QWORD *)((char *)&unk_100000028 + v2);
     }
     *(_QWORD *)((char *)&unk_100000000 + (unsigned int)v4) = 0;
     ((void (__usercall *)())runtime.gorecover)();
     ```
   - Dẫn chứng cụ thể trên `reflect.addReflectOff` (`gowasm-balls` @ `0x12f408`, tương ứng `wasm_func_1301` @ `0x1048f0` trên `main.wasm`, từ `FAIL code=-31` tại `0x12f556` → `OK` 109 dòng):
     ```c
     ((void (__usercall *)())runtime.makemap_small)();
     v3 = *(_QWORD *)((char *)&unk_100000000 + v2);
     if ( LODWORD(qword_100150E50[7308]) != 0 )
     {
       *(_QWORD *)&byte_100000038[v2 + 16] = v3;
       *(_QWORD *)((char *)&unk_100000030 + v2) = qword_100150E50[76];
       __SET_PAIR__(v8, v9, runtime.gcWriteBarrier2());
       v6 = v5;
       v2 = wasmhex_global_0;
       *(_QWORD *)((char *)&unk_100000000 + v6) = *(_QWORD *)&byte_100000038[wasmhex_global_0 + 16];
       *(_QWORD *)((char *)&unk_100000008 + v6) = *(_QWORD *)((char *)&unk_100000030 + v2);
       v3 = *(_QWORD *)&byte_100000038[v2 + 16];
     }
     qword_100150E50[76] = v3;
     ((void (__usercall *)())runtime.makemap_small)();
     ```

### T0 rev6 — M4-S2 hiển thị lời gọi C++ sạch (C1, C3, C7-display, C8)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `584ca90ec4ec18bdd9f68ea1e1df5827` (`WASMHEX_BUILD_MARKER_M4_S2_CPP_CALL_DISPLAY`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `12ac4bebde7cc91898e9e76b51b61947` |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3→rev4→rev5→rev6, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**100 / 100 hàm byte-identical 100%** so với rev4 & rev5) |
| main100 `sha256` log đầy đủ (rev6) | `ddd07004a7fa71656e6bd30231acbb92a1a6c1781f335400f29e98a657c1b1e0` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `7e2dbbe8e45dd795eccf8193611700e7895f3a136b673b382b4c05e4bb207754` (**BẤT BIẾN 100/100 hàm byte-identical so với rev4 & rev5**) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev6) | `887767c426888f38db8bd4090176fb9b455e2c5967f25207d9fa6c942ba5cbfc` |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev6) | `6cf5fc9161997d70d1890a841b04867fc16bc09bb99748cc5d86ed093f8da3f1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev3, rev4 & rev5**) |

**Bảng phân loại thay đổi (rev6 — M4-S2 `native/wasmhex/decompiler/rewriter_idiom.cpp`):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | Đổi hợp lệ do M4-S2 | Ghi chú |
|---|---:|---:|---:|---|
| `main.wasm` (`first100`) | 100 | **100 / 100** (`100 OK`) | 0 | Bất biến 100% (`7e2dbbe8…`, Go không bị chạm) |
| `gowasm-balls` (5 hàm smoke) | 5 | **5 / 5** (`5 OK`) | 0 | Bất biến 100% (`6cf5fc91…`, Go không bị chạm) |
| `tests/golden/` (12 Go fixtures) | 52 | **52 / 52** (`52 PASS`) | 0 | Bất biến 100% (`0` repair) |
| `tests/golden/` (3 table fixtures: `link_multi_table_a.o`, `s1_import_table_arg_clobber`, `link_table_reloc.o`) | 7 | **2 / 7** (`wasm_func_0` ×2) | **5 / 7** (`read_b`, `s1_first_arg_survives`, `s1_swapped`, `s1_loaded_arg`, `wasm_func_1`: mỗi hàm `8 → 7` dòng) | C8 gộp `v_fn = wasm_table_lookup_...(idx); return v_fn(...)` thành `return __indirect_function_table[idx](...)` ở chế độ `bounds=hidden` |
| `tests/integration/golden/mergeProcessed_baseline.txt` | 1 | — | **1 / 1** (`6` call sites C3, giữ nguyên `lines=307 stack=0 helpers=3 coerce=0 goto=13 lvars=64`) | C3 chuyển `(&wasm_func_32420)(...)` ×3, `(&std::vector<...>::__throw_length_error[abi:ne180100])(...)` ×2, `(&_cxa_throw_bad_array_new_length)()` ×1 thành lời gọi trực tiếp |
| `duckdb.wasm` (`stride=100`) | 634 | **634 / 634** (`634 OK`) | C1/C3/C7/C8 làm sạch văn bản hiển thị | F5-subset bất biến 100% (`5f728cb0…`) cả `hidden` và `explicit` |

**Cơ chế & dẫn chứng cho từng lớp thay đổi (M4-S2):**
1. **C1 — Tách kiểu trả về đứng đầu khỏi tên hàm C++ đã demangle (`split_demangled_callee_span`):**
   - Quét ở độ sâu `0` bên ngoài `<...>`, `(...)`, `[...]` (bỏ qua token `operator...`), cắt bỏ mọi phần kiểu trả về phía trước định danh có qualifier cuối cùng trước `(`.
   - Dẫn chứng trên `ExtractJoinRelations` (`0x188048`, `26 → 0` chỗ còn kiểu trả về trong tên hàm):
     ```diff
     -  v454 = duckdb::LogicalProjection & duckdb::LogicalOperator::Cast<duckdb::LogicalProjection>((int)v734);
     +  v454 = duckdb::LogicalOperator::Cast<duckdb::LogicalProjection>((int)v734);
     -  v128 = duckdb::RelationStats* std::__2::vector<duckdb::RelationStats,std::__2::allocator<duckdb::RelationStats>>::__push_back_slow_path<duckdb::RelationStats const&>((int)&v714, (int)&v705);
     +  v128 = std::__2::vector<duckdb::RelationStats,std::__2::allocator<duckdb::RelationStats>>::__push_back_slow_path<duckdb::RelationStats const&>((int)&v714, (int)&v705);
     ```
2. **C7-display — Cắt hậu tố đụng độ `_[0-9]+`, chấp nhận `GOT.func.orig$_Z`, và chú thích ICF từ blob `'E'` multi-export:**
   - Khi `demangle_name` thất bại trên tên `_Z...`, cắt hậu tố `_[0-9]+` do `used_global_names` sinh ra rồi thử lại (bảo toàn dấu `_` cuối của `T0_` trong Itanium ABI); mở khóa toàn bộ `63 → 0` lời gọi `invoke_vi(GOT.func._ZNK...CheckValidEv_427, ...)` và `invoke_...(GOT.func._ZNSt3__212__hash_table..._2082, ...)` trong `ExtractJoinRelations`.
   - `annotate_icf_group` tiêu thụ danh sách multi-export từ netnode blob `'E'` (v1) của S1 và gắn chú thích cho cả mục tiêu của `invoke_*`:
     - `duckdb::DuckDB::~DuckDB` (`0x1efba8`): `// wasmhex ICF-folded with: _ZN6duckdb10shared_ptrINS_21DynamicTableFilterSetELb1EED2Ev _ZN6duckdb10shared_ptrINS_17DynamicFilterDataELb1EED2Ev _ZN6duckdb10shared_ptrINS_18BoundParameterDataELb1EED2Ev _ZN6duckdb10shared_ptrINS_6BinderELb1EED2Ev (+166 more)`
     - `duckdb::optional_ptr<duckdb::JoinRelationSet,true>::CheckValid` (`0x18261b`): `// wasmhex ICF-folded with: _ZNK6duckdb12optional_ptrINS_15QueryGraphEdges9QueryEdgeELb1EE10CheckValidEv _ZNK6duckdb12optional_ptrINS_13RelationStatsELb1EE10CheckValidEv _ZNK6duckdb12optional_ptrINS_15LogicalOperatorELb1EE10CheckValidEv _ZNK6duckdb12optional_ptrINS_10FilterInfoELb1EE10CheckValidEv (+172 more)`
3. **C3 — Lời gọi trực tiếp thay cho `(&f)(...)` ở mọi số đối số (`rewrite_address_of_callee`):**
   - Dựng biểu thức thay thế qua `call_helper` cho mọi hàm (`wasm_func_*`, `_Z...` đã demangle, import `__cxa_throw_bad_array_new_length`), triệt tiêu hoàn toàn nguy cơ `INTERR 50718` do lệch `pointee->type` / `call->a->functype` (`(&wasm_func_`: `2 → 0` ở `ExtractJoinRelations`, `3 → 0` ở `mergeProcessed`; `(&_cxa_`: `3 → 0` và `1 → 0`; `(&std::`: `3 → 0` và `2 → 0`).
4. **C8 — Gộp tra cứu bảng hàm `wasm_table_lookup_*` thành `__indirect_function_table[idx](args)` + chú thích `// vtable+<off>` (chỉ ở `bounds=hidden`):**
   - Trong `ExtractJoinRelations` (`21 → 0` `wasm_table_lookup_`, giảm `60` dòng từ `2.996 → 2.936`) và `PipelineTask::ExecuteTask` (`1 → 0`, dòng 128):
     ```diff
     -  v122 = (void (__usercall *)(int@<r0>))wasm_table_lookup_table0_type0_size40267_i32(*(_DWORD *)(*v121 + 4));
     -  v122((int)v121);
     +  __indirect_function_table[*(_DWORD *)(*v121 + 4)]((int)v121);// vtable+4
     -  v26 = (void (__usercall *)(int@<r0>))wasm_table_lookup_table0_type0_size40267_i32(*(_DWORD *)(*(_DWORD *)v16 + 8));
     -  v26((int)v17);
     +  __indirect_function_table[*(_DWORD *)(*(_DWORD *)v16 + 8)]((int)v17);// vtable+8
     ```

### T0 rev7 — M4-S3 wrapper legalize-i64 + dọn giao thức EH (C4, C5)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `f339ff886d62557817ea3a604287c19b` (`WASMHEX_BUILD_MARKER_M4_S3_LEGALIZE_I64_EH_CLEANUP`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `12ac4bebde7cc91898e9e76b51b61947` |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3→rev4→rev5→rev6→rev7, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**100 / 100 hàm byte-identical 100%** so với rev4, rev5 & rev6) |
| main100 `sha256` log đầy đủ (rev7) | `ddd07004a7fa71656e6bd30231acbb92a1a6c1781f335400f29e98a657c1b1e0` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `7e2dbbe8e45dd795eccf8193611700e7895f3a136b673b382b4c05e4bb207754` (**BẤT BIẾN 100/100 hàm byte-identical so với rev4, rev5 & rev6**) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev7) | `887767c426888f38db8bd4090176fb9b455e2c5967f25207d9fa6c942ba5cbfc` |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev7) | `6cf5fc9161997d70d1890a841b04867fc16bc09bb99748cc5d86ed093f8da3f1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev3, rev4, rev5 & rev6**) |

**Bảng phân loại thay đổi (rev7 — M4-S3 `native/wasmhex/decompiler/rewriter_eh.cpp`):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | Đổi hợp lệ do M4-S3 | Ghi chú |
|---|---:|---:|---:|---|
| `main.wasm` (`first100`) | 100 | **100 / 100** (`100 OK`) | 0 | Bất biến 100% (`7e2dbbe8…`, Go không bị chạm) |
| `gowasm-balls` (5 hàm smoke) | 5 | **5 / 5** (`5 OK`) | 0 | Bất biến 100% (`6cf5fc91…`, Go không bị chạm) |
| `tests/golden/` (12 Go fixtures) | 52 | **52 / 52** (`52 PASS`) | 0 | Bất biến 100% (`0` repair) |
| `tests/integration/golden/mergeProcessed_baseline.txt` | 1 | — | **1 / 1** (gắn 1 chú thích landing pad tại `LABEL_60`; giữ nguyên 6 chỉ số `lines=307 stack=0 helpers=3 coerce=0 goto=13 lvars=64`) | C5.2 gắn `// wasmhex: EH landing pad (catch + cleanup + resume)` trên khối `v56 = __cxa_find_matching_catch_2(); ... __resumeException(v56);` |
| `duckdb.wasm` (`stride=100`) | 634 | **634 / 634** (`634 OK`) | C4/C5 làm sạch wrapper `legalize-i64`, chuỗi `||`/comma, và chú thích landing pad | F5-subset bất biến 100% (`5f728cb0…`) cả `hidden` và `explicit` |

**Cơ chế & dẫn chứng cho từng lớp thay đổi (M4-S3):**
1. **C4 — Mở rộng `is_invoke_helper_name` (`v/i/j/f/d`) và nhận diện wrapper `legalize-i64` theo thân hàm (`match_legalize_i64_wrapper_body`):**
   - Dựng chỉ mục lười theo DB bằng bộ giải mã chung `core/binary/instruction_stream`, chỉ nhận các hàm có hình dạng chuẩn chuyển tiếp `local.get 0..K-1` (tách tham số `i64` bằng `i32.wrap_i64; local.get p; i64.const 32; i64.shr_u; i32.wrap_i64` và/hoặc ghép kết quả trả về `i64` từ `tempRet0` bằng `i64.extend_i32_u; global.get G; i64.extend_i32_u; i64.const 32; i64.shl; i64.or`) quanh đúng một lệnh `call invoke_[vijfd]+` rồi kết thúc bằng `end` (`46 / 46` hàm trong dải `wasm_func_62929..62974` — `47 / 47` nếu tính `62929..62975` — và tổng cộng **187** hàm chuẩn toàn `duckdb.wasm` trong `wasm_func_62929..63131`).
   - Dọn sạch giao thức `__THREW__` quanh lời gọi wrapper và hạ callee wrapper về helper `invoke_*` tương ứng để `rewrite_invoke_got` của S2 hiển thị tên hàm gốc từ `GOT.func.orig$_Z...` (trong `ExtractJoinRelations`: `wasm_func_629[0-9][0-9]` giảm `15 → 0`, `GOT.func.orig$` giảm `15 → 0`, dòng giảm `2.932 → 2.851` = **−81 dòng**):
     ```diff
     -  v458 = (int *)GOT.mem.__THREW__;
     -  *(_DWORD *)GOT.mem.__THREW__ = 0;
     -  wasm_func_62934(GOT.func.orig$_ZN6duckdb15LogicalOperator23SetEstimatedCardinalityEy, v454, v716);
     -  v459 = *v458;
     -  *v458 = 0;
     -  if ( v459 != 1 )
     +  duckdb::LogicalOperator::SetEstimatedCardinality(v454, v716);
     +  if ( *(int *)GOT.mem.__THREW__ != 1 )
     ```
2. **C5.1 — Dọn giao thức `__THREW__` trong điều kiện `||`/`&&` và chuỗi toán tử phẩy (`clean_if_condition` / `clean_comma_chain`):**
   - Đệ quy qua các toán tử kết nối điều kiện (`||`, `&&`, `!`) để thay thế phép đọc biến tạm `read_lvar` ở nhánh trái bằng `*(int *)GOT.mem.__THREW__`, đồng thời trải phẳng chuỗi `cot_comma` ở nhánh phải để xóa các phép gán `*(_DWORD *)GOT.mem.__THREW__ = 0`, `*v = 0` và các phép gán biến tạm khi toàn bộ lượt dùng của chúng biến mất (trong `ExtractJoinRelations`: `*(_DWORD *)GOT.mem.__THREW__ = 0` giảm `19 → 0`; trong `AddRelationTdom` @ `0x193e2b`: dòng giảm `185 → 176` = **−9 dòng**):
     ```diff
     -  if ( v10 == 1
     -    || (v11 = (int *)GOT.mem.__THREW__,
     -        *(_DWORD *)GOT.mem.__THREW__ = 0,
     -        v12 = invoke_iii(&wasm_func_3564, (int)&v43, (int)&v40),
     -        v13 = v12,
     -        v14 = v13,
     -        v15 = *v11,
     -        *v11 = 0,
     -        v15 == 1) )
     +  if ( *(int *)GOT.mem.__THREW__ == 1
     +    || (v12 = wasm_func_3564((int)&v43, (int)&v40), v13 = v12, v14 = v13, *(int *)GOT.mem.__THREW__ == 1) )
     ```
3. **C5.2 — Chú thích khối landing pad EH cuối (`annotate_landing_pad_block`):**
   - Gắn chú thích `// wasmhex: EH landing pad (catch + cleanup + resume)` đúng một lần trên mỗi khối landing pad cuối chứa `__cxa_find_matching_catch_*` dẫn tới `__resumeException` (`ExtractJoinRelations`: `1` khối tại `LABEL_113`; `AddRelationTdom`: `2` khối; `AddRelation`: `2` khối; `ExecuteTask`: `3` khối; `mergeProcessed`: `1` khối tại `LABEL_60`).

### T0 rev8 — M4-S4 tách C-frame zero-store + cách ly kreg (C9a, C9b) và các idiom libc++ (C6)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `f482ad3d825ca0c5ab155d1c03011ade` (`WASMHEX_BUILD_MARKER_M4_S4_CFRAME_SPLIT_KREG_LIBCXX`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `603f13fe4cb688fb925ffbb949778e79` (`WASMHEX_BUILD_MARKER_M4_S4_CFRAME_SPLIT_KREG_LIBCXX`) |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3→rev4→rev5→rev6→rev7→rev8, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**100 / 100 hàm byte-identical 100%** so với rev4, rev5, rev6 & rev7) |
| main100 `sha256` log đầy đủ (rev8) | `ddd07004a7fa71656e6bd30231acbb92a1a6c1781f335400f29e98a657c1b1e0` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `7e2dbbe8e45dd795eccf8193611700e7895f3a136b673b382b4c05e4bb207754` (**BẤT BIẾN 100/100 hàm byte-identical so với rev4, rev5, rev6 & rev7**) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev8) | `887767c426888f38db8bd4090176fb9b455e2c5967f25207d9fa6c942ba5cbfc` |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev8) | `6cf5fc9161997d70d1890a841b04867fc16bc09bb99748cc5d86ed093f8da3f1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev3, rev4, rev5, rev6 & rev7**) |

**Bảng phân loại thay đổi (rev8 — M4-S4 `lower_memory.cpp`, `registers.cpp`, `abi.cpp`, `rewriter_libcxx.cpp`):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | Đổi hợp lệ do M4-S4 | Ghi chú |
|---|---:|---:|---:|---|
| `main.wasm` (`first100`) | 100 | **100 / 100** (`100 OK`) | 0 | Bất biến 100% (`7e2dbbe8…`, Go không có `has_c_frame`) |
| `gowasm-balls` (5 hàm smoke) | 5 | **5 / 5** (`5 OK`) | 0 | Bất biến 100% (`6cf5fc91…`, Go không có `has_c_frame`) |
| `tests/golden/` (12 Go fixtures + 56 `kr0` fixtures + 10 C/DWARF fixtures) | 487 | **487 / 487** | 0 | Bất biến 100% (`0` repair, `EXPECT_TOTAL pass=6 fail=0`) |
| `tests/integration/golden/mergeProcessed_baseline.txt` | 1 | — | **1 / 1** (`lines: 307 → 265`, `goto: 13 → 8`, `lvars: 64 → 68`, `stack=0`, `helpers=3`, `coerce=0`) | C9a tách `i64.store 0` trên `v60`/`v59` (`LODWORD/HIDWORD` trên vector vars `8 → 0`); C6.4 gập 54 dòng realloc thành `std::vector<T>::push_back(&v60, v59);` |
| `duckdb.wasm` (`stride=100`) | 634 | **634 / 634** (`634 OK`) | C9a + C9b + C6 làm sạch `LODWORD`/`HIDWORD`, `HIDWORD(v5)` và 4 idiom libc++ | F5-subset bất biến 100% (`5f728cb0…`) cả `hidden` và `explicit` |

**Cơ chế & dẫn chứng cho từng lớp thay đổi (M4-S4):**
1. **C9a — Tách phép ghi hằng `0` 8-byte vào C shadow-stack thành hai phép ghi 4-byte (`decompiler/lower_memory.cpp:emit_c_frame_scalar_memory`):**
   - Khi một phép `i64.store` ghi hằng số `0` vào một ô C-frame nội bộ (`[effective_offset]`), phát hai lệnh `m_mov` 4-byte (`#0.4 -> [effective_offset].4` và `#0.4 -> [effective_offset + 4].4`) với `clr_combinable()` (`IPROP_DONT_COMB`) để ngăn bộ kết hợp lệnh của Hex-Rays gộp ngược thành một biến 8-byte `__int64`.
   - Trong `mergeProcessed` (`0x1027ee2`): `v60` và `v59` tách thành các con trỏ `char *` 4-byte (`v60, v61, v62` và `v63, v64`), loại bỏ toàn bộ `8 → 0` lượt `LODWORD(v60)` / `HIDWORD(v60)` / `HIDWORD(v20)` (tổng `LODWORD/HIDWORD` toàn hàm giảm `10 → 2`, chỉ còn trên `v59 = *(_QWORD *)v20`).
2. **C9b — Cách ly các kreg hẹp (`< 8` byte) trong hàm có C-frame bằng khoảng đệm 4-byte (`decompiler/registers.cpp`, `ida/abi.cpp`):**
   - Giữ `set_reg2` trong `ida/abi.cpp` (bởi bảng thanh ghi processor ánh xạ `r0..r63` vào các thanh ghi 4-byte và `ida/metadata.cpp:make_regloc_mop` yêu cầu `loc.is_reg2()` cho giá trị 8-byte), đồng thời chèn khoảng đệm `cdg.mba->alloc_kreg(4)` sau mỗi kreg `< 8` byte trong `make_local_storage`, `make_operand_register`, `make_scratch_register` khi `has_c_frame == true`.
   - Ngăn hoàn toàn việc Hex-Rays gộp hai lệnh `m_mov` 4-byte liền kề (`this` tại `kr00_4` và `a2` tại `kr04_4`, hoặc hai phép `i32.load` liên tiếp vào `kr00_4`/`kr04_4`) thành biến 8-byte `__int64 v5 // kr00_8`.
   - Trong `ExtractJoinRelations` (`0x188048`): `HIDWORD(v5)` giảm **`94 → 0`** (trong đó 14 cụm `HIDWORD(v5) = GOT.mem.__THREW__; ... *(_DWORD *)HIDWORD(v5) = 0;` biến thành biến 4-byte chuẩn và được `wasmhex_eh_rewriter_t` của S3 tự động dọn sạch **`14 → 0`**); tổng `LODWORD`/`HIDWORD` toàn hàm giảm **`190 → 6`** (**−96,8%**).
3. **C6 — Bốn bộ nhận diện idiom libc++ fail-closed ở chế độ `bounds=hidden` (`decompiler/rewriter_libcxx.cpp`):**
   - **C6.1 (`unique_ptr` virtual delete qua `vtable+4`):** `if (p != 0) __indirect_function_table[*(*p + 4)](p);` → `delete(p); // std::default_delete` (`ExtractJoinRelations`: `17` matched / `5` skipped; `ExecuteTask`: `0` matched / `1` skipped do `vtable+8`).
   - **C6.2 (`std::string` SSO destructor):** `if (*(char *)(s + 11) < 0) free(*(void **)s);` và dạng biến stack tại `+11` (1B) / `+8` (4B MSB sign) → `std::__1::string::~string(s)` (`ExtractJoinRelations`: `18` matched / `0` skipped; `ExecuteTask`: `1` matched / `0` skipped).
   - **C6.3 (`std::vector<std::string>` destructor loop):** vòng lặp lùi `-12` byte kiểm tra `*((char *)it - 1) < 0` + `free(*(it - 3))` kết thúc bằng `end = begin; free(begin);` → `std::vector<std::string>::~vector(v)` (`ExtractJoinRelations`: `18` matched / `3` skipped; `AddRelationTdom`: `1` matched / `0` skipped).
   - **C6.4 (`std::vector<T>::push_back` inlined realloc ~55 dòng):** kiểm tra đầy đủ capacity check, `__throw_length_error`, `__cxa_throw_bad_array_new_length`, `_Znwm`, vòng lặp chép ngược, cập nhật 3 trường `{begin, end, cap}`, `free(old)`, không có incoming `goto` từ ngoài vùng và biến tạm không thoát ra ngoài → `std::vector<T>::push_back(&vec, val)` (`ExtractJoinRelations`: `2` matched / `1` skipped do `LABEL_651` có 42 incoming `goto`; `mergeProcessed`: `1` matched / `0` skipped).
   - Tổng số dòng giảm trên `ExtractJoinRelations`: `2.851 → 2.411` (**−440 dòng**, **−15,4%**); trên `mergeProcessed`: `308 → 266` (**−42 dòng**, **−13,6%**); trên `AddRelationTdom`: `176 → 160` (**−16 dòng**).

### T0 rev9 — G6 thu gọn khối write barrier Go thành phép gán thường + chú thích `// go: write barrier`

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `68958fe6f0c4c4f8ff11c114a0d3ea07` (`WASMHEX_BUILD_MARKER_G6_WRITE_BARRIER_COLLAPSE`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `3386718e32dd38c5e4cc59426fe8272b` |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3→rev4→rev5→rev6→rev7→rev8→rev9, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**15 / 100 hàm đổi hợp lệ** thuộc duy nhất lớp `wb-collapse`; **85 / 100 hàm còn lại byte-identical 100%** so với rev4–rev8) |
| main100 `sha256` log đầy đủ (rev9) | `0035a77d257df3d7ce1be16e643f95352f3e118a02b1d41f53c02e0d8aeb0c6e` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `b21d43f092fc1eec345372b4fef64c1185b3ca83f0a040b88aba34be08df45a4` (rev8: `7e2dbbe8e45dd795eccf8193611700e7895f3a136b673b382b4c05e4bb207754`) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev9) | `acce0706e5cf661a273b49cc128cf5543f4c50f1077e4c4aa682ada915bdf1b8` |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev9) | `a73e49e5be42f6da95923d7ef18d183202798579149273f3849d15fd7b08c6d1` (rev8: `6cf5fc9161997d70d1890a841b04867fc16bc09bb99748cc5d86ed093f8da3f1`; **1 / 5 hàm đổi hợp lệ** là `main.main` thuộc lớp `wb-collapse`, **4 / 5 hàm còn lại byte-identical 100%**) |
| 4 cặp hàm báo cáo TRIM (`main.wasm` / `gowasm-balls`) | **75 / 76 site matched, 1 site skipped** (`reflect.addReflectOff` / `wasm_func_1301` site 3 do `if (v14 == 0) sigpanic()` trong thân barrier); tổng dòng giảm `3.285 → 2.295` (**−990 dòng**, **−30,1%**) trên `gowasm-balls` và `3.039 → 2.050` (**−989 dòng**, **−32,5%**) trên `main.wasm` |

**Bảng phân loại thay đổi (rev9 — G6 `native/wasmhex/decompiler/rewriter_go.cpp`):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | Đổi hợp lệ lớp `wb-collapse` | Site matched / skipped | Tổng dòng (`OK`) |
|---|---:|---:|---:|---:|---:|
| `main.wasm` (`first100`) | 100 | **85 / 100** (`85 OK`) | **15 / 100** (`wasm_func_89, 90, 93, 94, 95, 97, 102, 104, 106, 108, 109, 111, 113, 116, 118`) | **36 matched / 2 skipped** (`wasm_func_43` `extra_side_effect_in_body`; `wasm_func_111` site 2 `missing_post_store`) | `6.505 → 5.962` (**−543**, **−8,3%**; riêng 15 hàm đổi: `2.660 → 2.117`, **−20,4%**) |
| `gowasm-balls` (5 hàm smoke) | 5 | **4 / 5** (`main.loop`, `main.__Ball_.draw`, `main.main.func1`, `go_buildid`) | **1 / 5** (`main.main`: `308 → 240` dòng, **−68 dòng**, **−22,1%**) | **4 matched / 0 skipped** (3× `gcWriteBarrier2`, 1× `gcWriteBarrier4`) | `524 → 456` (**−68**, **−13,0%**) |
| 4 hàm TRIM `gowasm-balls` (`initMetrics`, `addReflectOff`, `popTail`, `OnceFunc.func1.1.1`) | 4 | 0 | **4 / 4** (`initMetrics`: `3.034 → 2.116`, `addReflectOff`: `109 → 81`, `popTail`: `81 → 68`, `OnceFunc`: `61 → 30`) | **75 matched / 1 skipped** (`addReflectOff` site 3 `extra_side_effect_in_body`) | `3.285 → 2.295` (**−990**, **−30,1%**) |
| 4 hàm TRIM `main.wasm` (`wasm_func_361, 1301, 1547, 2425`) | 4 | 0 | **4 / 4** (`361`: `2.791 → 1.873`, `1301`: `109 → 81`, `1547`: `79 → 66`, `2425`: `60 → 30`) | **75 matched / 1 skipped** (`1301` site 3 `extra_side_effect_in_body`) | `3.039 → 2.050` (**−989**, **−32,5%**) |
| `tests/golden/` (12 Go fixtures) | 52 | **52 / 52** (`52 PASS`) | 0 | 0 | Bất biến 100% (`0` repair) |
| `duckdb.wasm` (`stride=100`) | 634 | **634 / 634** (`634 OK`) | 0 | 0 | F5-subset bất biến 100% (`5f728cb0…`) cả `hidden` và `explicit` |

**Cơ chế & dẫn chứng cho lớp `wb-collapse` (G6 `rewriter_go.cpp`):**
1. **Nhận diện hình dạng write barrier fail-closed ở `CMAT_FINAL` (`try_collapse_wb_if` / `go_wb_detail::verify_wb_candidate`):**
   - Khối `if` không có nhánh `else`, không có nhãn nhảy nội bộ, điều kiện kiểm tra cờ tĩnh `runtime.writeBarrier.enabled != 0` (trực tiếp hoặc qua biến cờ cục bộ `v_flag = (writeBarrier.enabled == 0); ... if (!v_flag)` trong cùng khối), thân khối chỉ chứa: (i) các phép spill thuần vào frame `[SP + off]`, (ii) đúng 1 lời gọi `runtime.gcWriteBarrier{1..8}` (chứng minh qua tên pclntab hoặc qua cặp thân hàm `gcWriteBarrier{1..8} -> gcWriteBarrier` đối với module bị strip tên như `main.wasm`), (iii) sao chép con trỏ buffer + làm tươi `SP = wasmhex_global_0`, (iv) đúng `N` phép ghi liên tiếp `buffer[0 .. N-1]` tại `buf + 0, 8, ..., (N-1)*8`, (v) nạp lại biến cục bộ từ đúng ô frame `[SP + off]`, biến tạm không thoát ra ngoài, và ngay sau khối `if` là phép gán con trỏ thật sự `*dst = val` khớp với giá trị/địa chỉ của barrier.
   - Khi chứng minh thành công: xóa khối `if` (cùng định nghĩa biến cờ chết `v_flag` và ẩn khai báo các biến tạm `eax`/`ecx`/`buf` không còn lượt dùng qua `clear_used()`), gắn chú thích `// go: write barrier` vào lệnh gán con trỏ. Nếu bất kỳ điều kiện nào vi phạm (như site 3 của `reflect.addReflectOff` / `wasm_func_1301` có lệnh `if (v14 == 0) runtime.sigpanic();` bên trong thân `if`), giữ nguyên toàn bộ khối (fail-closed).
2. **Dẫn chứng cụ thể trên `reflect.addReflectOff` (`gowasm-balls` @ `0x12f408`, `109 → 81` dòng) và `sync.OnceFunc.func1.1.1` (`0x142ba0`, `61 → 30` dòng):**
   ```diff
   -    if ( LODWORD(qword_100150E50[7308]) != 0 )
   -    {
   -      *(_QWORD *)&byte_100000038[v2 + 16] = v3;
   -      *(_QWORD *)((char *)&unk_100000030 + v2) = qword_100150E50[76];
   -      __SET_PAIR__(v4, v5, runtime.gcWriteBarrier2());
   -      v6 = v5;
   -      v2 = wasmhex_global_0;
   -      *(_QWORD *)((char *)&unk_100000000 + v6) = *(_QWORD *)&byte_100000038[wasmhex_global_0 + 16];
   -      *(_QWORD *)((char *)&unk_100000008 + v6) = *(_QWORD *)((char *)&unk_100000030 + v2);
   -      v3 = *(_QWORD *)&byte_100000038[v2 + 16];
   -    }
   -    qword_100150E50[76] = v3;
   +    qword_100150E50[76] = v3;                   // go: write barrier
   ```
   ```diff
   -  v3 = LODWORD(qword_100150E50[7308]) == 0;
      v4 = *(_QWORD *)((char *)&unk_100000008 + (unsigned int)wasmhex_global_1);
      *(_QWORD *)((char *)&unk_100000028 + v2) = v4;
   -  if ( !v3 )
   -  {
   -    *(_QWORD *)&byte_100000014[v2 + 4] = *(_QWORD *)((char *)&unk_100000000 + (unsigned int)v4);
   -    __SET_PAIR__(v5, v6, runtime.gcWriteBarrier1());
   -    v7 = v6;
   -    v2 = wasmhex_global_0;
   -    *(_QWORD *)((char *)&unk_100000000 + v7) = *(_QWORD *)&byte_100000014[wasmhex_global_0 + 4];
   -    v4 = *(_QWORD *)((char *)&unk_100000028 + v2);
   -  }
   -  *(_QWORD *)((char *)&unk_100000000 + (unsigned int)v4) = 0;
   +  *(_QWORD *)((char *)&unk_100000000 + (unsigned int)v4) = 0;// go: write barrier
   ```

### T0 rev10 — G6-runtime hạ bậc `runtime.systemstack` fast-path + `wasm_pc_f_loop` thành lời gọi gián tiếp O(1) (xóa sạch 2 lỗi `FAIL -22` cuối cùng trên Go)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `38a81451f4a0651303d2f36b1c5c01ff` (`WASMHEX_BUILD_MARKER_G6_RUNTIME_SYSTEMSTACK_PCF_LOOP`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `6ecc5ed083cd09a951f17d548cef7be6` (`WASMHEX_BUILD_MARKER_G6_RUNTIME_SYSTEMSTACK_PCF_LOOP`) |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev2→rev3→rev4→rev5→rev6→rev7→rev8→rev9→rev10, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**100 / 100 hàm byte-identical 100%** so với rev9) |
| main100 `sha256` log đầy đủ (rev10) | `0035a77d257df3d7ce1be16e643f95352f3e118a02b1d41f53c02e0d8aeb0c6e` (**BẤT BIẾN 100% so với rev9**) |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `b21d43f092fc1eec345372b4fef64c1185b3ca83f0a040b88aba34be08df45a4` (**BẤT BIẾN 100/100 hàm byte-identical so với rev9**) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev10) | `acce0706e5cf661a273b49cc128cf5543f4c50f1077e4c4aa682ada915bdf1b8` (**BẤT BIẾN 100% so với rev9**) |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev10) | `a73e49e5be42f6da95923d7ef18d183202798579149273f3849d15fd7b08c6d1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev9**) |
| **`gowasm-balls` toàn module (1.759 hàm)** | **`1.759 / 1.759 OK` (100,00%, `0 FAIL`)** — xóa sạch 2 hàm `FAIL -22` cuối cùng (`runtime.systemstack` @ `0x135742`: `preopt qty=3.537 → 15`, `glbopt qty=3.531 → 10`, `1,24 s → 52 ms`, `51` dòng; `wasm_pc_f_loop` @ `0x137b27`: `preopt qty=3.529 → 7`, `glbopt qty=3.526 → 5`, `12,80 s → 1,8 ms`, `10` dòng) |
| **`main.wasm` 2 hàm đối ứng (`wasm_func_1336` & `wasm_func_1387`)** | **`2 / 2 OK`** (nâng tổng toàn module lên **3.255 / 3.255 `OK` = 100,00%, `0 FAIL`**; `wasm_func_1336` @ `0x10a244`: `preopt qty=15`, `glbopt qty=10`, `3,64 s → 120 ms`, `51` dòng; `wasm_func_1387` @ `0x10c669`: `preopt qty=7`, `glbopt qty=5`, `46,73 s → 1,8 ms`, `10` dòng) |

**Bảng phân loại thay đổi (rev10 — G6-runtime `core/toolchains/go/ir.hpp`, `core/toolchains/go/ir.cpp`, `decompiler/filter.hpp`, `decompiler/filter.cpp`, `decompiler/lower_calls.cpp`):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | `new-decompile-success` (`FAIL -22` → `OK`) | `switch (a1)` / `STACK[` / `JUMPOUT` | Ghi chú |
|---|---:|---:|---:|---:|---|
| `main.wasm` (`first100`) | 100 | **100 / 100** (`100 OK`) | 0 (trong `first100`) | 0 / 0 / 0 | Bất biến 100% (`b21d43f0…`, `5.962` dòng) |
| `main.wasm` (`wasm_func_1336`, `wasm_func_1387`) | 2 | — | **2 / 2** (`wasm_func_1336`: `51` dòng; `wasm_func_1387`: `10` dòng) | 0 / 0 / 0 | Toàn module đạt **3.255 / 3.255 `OK` (100%)** |
| `gowasm-balls` (5 hàm smoke) | 5 | **5 / 5** (`5 OK`) | 0 (trong 5 hàm smoke) | 0 / 0 / 0 | Bất biến 100% (`a73e49e5…`, `456` dòng) |
| `gowasm-balls` (toàn module) | 1.759 | **1.757 / 1.759** | **2 / 1.759** (`runtime.systemstack`: `51` dòng; `wasm_pc_f_loop`: `10` dòng) | 0 / 0 / 0 | Toàn module đạt **1.759 / 1.759 `OK` (100%)**, `time_max = 1,75 s` (`runtime.initMetrics`) |
| `tests/golden/` (12 Go fixtures) | 52 | **52 / 52** (`52 PASS`) | 0 | 0 / 0 / 0 | Bất biến 100% (`0` repair) |
| `duckdb.wasm` (`stride=100`) | 634 | **634 / 634** (`634 OK`) | 0 | 0 / 0 / 0 | F5-subset bất biến 100% (`5f728cb0…`) cả `hidden` và `explicit` |

**Cơ chế & dẫn chứng cho lớp `new-decompile-success` (`2 / 2` cặp hàm `runtime.systemstack` & `wasm_pc_f_loop`):**
1. **`runtime.systemstack` (`gowasm-balls` @ `0x135742` == `main.wasm` `wasm_func_1336` @ `0x10a244`, 311 B):**
   - Trong `runtime.systemstack(fn func())` (`cmd/internal/obj/wasm`), nhánh nhanh khi đã chạy trên `g0` (`g == m.g0` tại `@45`) gọi trực tiếp hàm closure `fn` mà không cấp phát khung return-address hay nhảy về `$unwindExit`:
     ```wasm
     local.get 2        ;; fn (closure ptr tại [SP+8])
     global.set 1       ;; CTXT = fn
     global.get 1
     i32.wrap_i64
     i64.load align=3 offset=0   ;; *(uint64*)CTXT (funcval)
     i64.const 16
     i64.shr_u
     i32.wrap_i64
     local.set 0
     i32.const 0
     local.get 0
     call_indirect 0 (type 0)    ;; @67
     global.get 0
     local.set 1
     return
     ```
   - Trước G6-runtime, bộ quét G4 chỉ khớp các lệnh `call_indirect` có cụm `SP -= 8; *(i64*)SP = ret_pc` phía trước và `br_if $unwindExit` phía sau (nhánh chậm `@248` khớp, nhưng nhánh nhanh `@67` bị bỏ qua và rơi vào `emit_sparse_indirect_call`, tạo chuỗi `m_jz` 3.537 khối ở `gowasm-balls` / 6.510 khối ở `main.wasm` → `MERR_COMPLEX`).
   - Giải pháp: `build_go_function_irs` nhận diện mẫu fast-path return `global.set 1; global.get 1; i32.wrap_i64; i64.load 3 0; i64.const 16; i64.shr_u; i32.wrap_i64; local.set 0; i32.const 0; local.get 0; call_indirect (type $pcb); global.get 0; local.set 1; return` trong các hàm đã chứng minh `pc_b_register_proof`, ghi cờ `is_pc_f_fast_return = true` vào `go_call_ir_t` (lưu trong bit `2` của `pcf_flags` thuộc tail `GOPC` v1), tiêu thụ 5 lệnh trích `PC_F` + `i32.const 0` + cặp refresh `global.get 0; local.set 1`, và hạ `call_indirect` thành đúng 1 lệnh `m_icall` trên `*(uint64*)CTXT`:
     ```c
     if ( wasmhex_global_2 == v4 )
     {
       wasmhex_global_1 = *(_QWORD *)((char *)&unk_100000008 + (unsigned int)wasmhex_global_0);
       return (*(int (__usercall **)@<r0>())((char *)&unk_100000000 + (unsigned int)v2))();
     }
     ```
2. **`wasm_pc_f_loop` (`gowasm-balls` @ `0x137b27` == `main.wasm` `wasm_func_1387` @ `0x10c669`, 41 B):**
   - `wasm_pc_f_loop` là vòng lặp trampoline ngoài cùng của runtime Go (`wasm_export_run` / `wasm_export_resume`), thuộc tập 18 hàm `notUsePC_B` có chữ ký `() -> void` và thân hàm cố định 24 lệnh (41 byte):
     ```wasm
     global.get 7 ; i32.eqz ; if
       loop
         global.get 0 ; i32.const 8 ; i32.sub ; i32.load16_u 1 0   ;; PC_B = *(uint16*)(SP - 8)
         global.get 0 ; i32.const 8 ; i32.sub ; i32.load 2 2       ;; PC_F = *(uint32*)(SP - 6)
         call_indirect 0 (type 0) ; drop
         global.get 7 ; i32.eqz ; br_if 0
       end
     end
     i32.const 0 ; global.set 7 ; return ; unreachable ; end
     ```
   - Giải pháp: `match_wasm_pc_f_loop_body` trong `core/toolchains/go/ir.cpp` chứng minh khớp từng lệnh một đối với toàn bộ 24 lệnh của thân hàm 41 byte dưới profile Go hiện đại đã xác thực (`build_ir`), ghi `is_pc_f_loop = true` và `pc_f_loop_call_offset = 23` vào `go_function_ir_t` (tail `GOLP` v1 trong blob `'x'`). Tại `lower_calls.cpp:emit_call`, điểm gọi `go_pcf_loop_call_ea` bỏ qua chuỗi `emit_sparse_indirect_call` và đi qua đường `checked_dynamic_indirect` (`wasm_table_lookup_table0_...` + `m_icall`), được C8 rút gọn thành lời gọi bảng hàm gián tiếp tường minh:
     ```c
     void __usercall wasm_pc_f_loop()
     {
       unsigned int v0; // kr10_4

       while ( wasmhex_global_7 == 0 )
         __indirect_function_table[*(_DWORD *)((char *)&unk_100000002 + (unsigned int)(wasmhex_global_0 - 8))](*(unsigned __int16 *)((char *)&unk_100000000 + (unsigned int)(wasmhex_global_0 - 8)));
       wasmhex_global_7 = 0;
     }
     ```

### T0 rev11 — M1 sửa `qstring::substr` end-index (3+ cờ `WASMHEX_DEBUG`) + G6 mở rộng `wb-collapse` cho nil-guard lặp (`if (dst == 0) runtime.sigpanic();`)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `8d98841e2c5473628d53ddd65b0d6858` (`WASMHEX_BUILD_MARKER_OPTIONS_SUBSTR_END_INDEX` + `WASMHEX_BUILD_MARKER_G6_WB_NIL_GUARD_COLLAPSE`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `d049e9618bc10c2679455812573fc209` (`WASMHEX_BUILD_MARKER_OPTIONS_SUBSTR_END_INDEX`) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev11, `634/634 OK` cả `hidden` và `explicit`)** |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev11) | `acce0706e5cf661a273b49cc128cf5543f4c50f1077e4c4aa682ada915bdf1b8` (**BẤT BIẾN 100% so với rev9 & rev10**) |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev11) | `a73e49e5be42f6da95923d7ef18d183202798579149273f3849d15fd7b08c6d1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev9 & rev10**) |
| 4 hàm TRIM `gowasm-balls` (`initMetrics`, `addReflectOff`, `popTail`, `OnceFunc.func1.1.1`) | **`76 / 76` site matched, `0` site skipped** (`reflect.addReflectOff` @ `0x12f408`: `matched 2 → 3`, `skipped 1 → 0`, dòng giảm `81 → 65` = **−16 dòng**); toàn module `gowasm-balls` (1.759 hàm): **`660` site matched / `28` site skipped** (`extra_side_effect_in_body` do `runtime.sigpanic` giảm **`182 → 0`**; 6 site `extra_side_effect_in_body` còn lại là lời gọi runtime thật sự `internal_runtime_maps.rand` ×1, `runtime.wbZero` ×4, `runtime.wbMove` ×1) |
| 4 hàm TRIM `main.wasm` (`wasm_func_361, 1301, 1547, 2425`) và `wasm_func_1806` (module bị strip pclntab) | Giữ nguyên **`75 / 76` site matched, `1` site skipped** trên 4 hàm TRIM và **`43 / 250` site matched, `207` site skipped** trên `wasm_func_1806` (theo đúng cổng tên pclntab/name-section `is_sigpanic_callee`: trên module bị strip, `wasm_func_671` không có tên `runtime.sigpanic` nên `205` site có `if (v == 0) wasm_func_671();` trong `wasm_func_1806` và `1` site trong `wasm_func_1301` tiếp tục fail-closed trung thực với lý do `extra_side_effect_in_body`, cùng `2` site `gcWriteBarrier4` trong `wasm_func_1806` thiếu `sp_refresh`) |

**Cơ chế & dẫn chứng cho Fix 1 (`loader/options.cpp`) và Fix 2 (`decompiler/rewriter_go.cpp`):**
1. **Fix 1 — Sửa đối số thứ hai của `_qstring::substr(pos, end_idx)` trong `loader/options.cpp:22-23`:**
   - Trong IDA SDK (`pro.h:3474`), `_qstring::substr(size_t pos, size_t n = npos)` nhận **`n` là chỉ số kết thúc (`end_idx`)**, không phải độ dài (`length`). Trước khi sửa, `value.substr(pos, comma - pos)` truyền độ dài vào vị trí `end_idx`, khiến mọi token từ vị trí thứ 3 trở đi có `pos > comma - pos` và trả về chuỗi rỗng (ví dụ với `WASMHEX_DEBUG="verify,phases,g2"`, cờ `"g2"` có `pos=14, comma=npos` hoặc khi có 4 cờ `"phases,verify,g2,go"`, cờ `"g2"` có `pos=14, comma=16, comma-pos=2 < 14` → bị cắt thành rỗng).
   - Giải pháp: truyền trực tiếp `comma` vào `value.substr(pos, comma)` thông qua helper `wasmhex::options::detail::parse_debug_categories`, kiểm thử đơn vị trong `test_leb.cpp` (`"a,b,c,d"` → 4 token `{"a","b","c","d"}`), và xác minh thực nghiệm qua `idat` với `WASMHEX_DEBUG="phases,verify,g2,go"` phát đầy đủ cả 4 nhóm log (`PHASE`, `WASMHEX_VERIFY`, `G2T`, `WASMHEX_GO_WB_*`).
2. **Fix 2 — Mở rộng `try_collapse_wb_if` trong `decompiler/rewriter_go.cpp` cho mẫu nil-check guard lặp (`if (dst == 0) runtime.sigpanic();`):**
   - Trong Go Wasm, khi gán con trỏ qua con trỏ có thể `nil`, trình biên dịch Go phát lệnh kiểm tra `if (dst == 0) runtime.sigpanic();` ở cả bên trong nhánh `if (writeBarrier.enabled)` (trước khi đọc giá trị cũ `*dst` để spill vào frame) lẫn ngay phía sau khối `if` (trước phép gán `*dst = val`).
   - Giải pháp: `extract_nil_guard_stmt` và `post_store_uses_guarded_ptr` cho phép thân `if (writeBarrier.enabled)` chứa tối đa 1 câu lệnh `if (ptr_expr == 0) runtime.sigpanic();` (chứng minh tên callee bằng `match_sigpanic_name("runtime.sigpanic")` từ pclntab/name-section, giống cổng `is_morestack_callee`) **chỉ khi** câu lệnh guard y hệt xuất hiện ngay sau khối `if` trước phép gán `*dst = val` trên cùng biểu thức con trỏ `dst`. Khi thu gọn, bản sao guard bên trong khối `if` bị xóa cùng khối `if`, còn câu lệnh guard phía sau và phép gán `*dst = val // go: write barrier` được giữ nguyên:
     ```diff
      // reflect.addReflectOff (gowasm-balls @ 0x12f408, site 3: 81 -> 65 dòng):
          ((void (__usercall *)())runtime.mapassign_fast32)();
          v14 = *(_QWORD *)&byte_100000000[v2 + 24];
     -    if ( LODWORD(qword_100151018[7251]) != 0 )
     -    {
     -      *(_QWORD *)&byte_100000000[v2 + 56] = v14;
     -      if ( v14 == 0 )
     -        ((void (__usercall *)())runtime.sigpanic)();
     -      *(_QWORD *)&byte_100000000[v2 + 48] = *(_QWORD *)&byte_100000000[(unsigned int)v14];
     -      __SET_PAIR__(v15, v16, runtime.gcWriteBarrier2());
     -      v17 = v16;
     -      v2 = wasmhex_global_0;
     -      *(_QWORD *)&byte_100000000[v17] = *(_QWORD *)&byte_100000000[wasmhex_global_0 + 88];
     -      *(_QWORD *)&byte_100000000[v17 + 8] = *(_QWORD *)&byte_100000000[v2 + 48];
     -      v14 = *(_QWORD *)&byte_100000000[v2 + 56];
     -    }
          if ( v14 == 0 )
            ((void (__usercall *)())runtime.sigpanic)();
     -    *(_QWORD *)&byte_100000000[(unsigned int)v14] = *(_QWORD *)&byte_100000000[v2 + 88];
     +    *(_QWORD *)&byte_100000000[(unsigned int)v14] = *(_QWORD *)&byte_100000000[v2 + 88];// go: write barrier
     ```

### T0 rev12 — G6 chứng minh hàm đích terminal noreturn thuần cấu trúc + đóng các khối cold-tail (`wasm_func_2422` `776,7s → 0,68s` & `wasm_func_1806` `576,0s → 1,38s` ở `structural`, `0` `goto`)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `79eea11e1d1c7389bf3d84e2b20b9685` (`WASMHEX_BUILD_MARKER_G6_PANIC_NORETURN_COLD_TAIL`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `d86048c5d0b745a3a67a36cb208c5af9` (`WASMHEX_BUILD_MARKER_G6_PANIC_NORETURN_COLD_TAIL`) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev12, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**`93 / 100` hàm byte-identical 100%** so với rev9–rev11; **`7 / 100` hàm đổi hợp lệ** thuộc duy nhất lớp `cold-tail-severed`: `wasm_func_25, 26, 27, 29, 33, 90, 110`, giảm `goto` từ `62 → 33` = **−46,8%**) |
| main100 `sha256` log đầy đủ (rev12) | `f0977dc90507a2e7f7da1fd6edcacb826130b443becc5a10786286ebc759237c` |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `8918d4c9a5e653b8cdd67233859eaa1816748f046b28939c543e39b4131dd7e9` (rev9–rev11: `b21d43f092fc1eec345372b4fef64c1185b3ca83f0a040b88aba34be08df45a4`) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev12) | `acce0706e5cf661a273b49cc128cf5543f4c50f1077e4c4aa682ada915bdf1b8` (**BẤT BIẾN 100% so với rev9, rev10 & rev11**) |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev12) | `a73e49e5be42f6da95923d7ef18d183202798579149273f3849d15fd7b08c6d1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev9, rev10 & rev11**) |
| **`gowasm-balls` toàn module (1.759 hàm)** | **`1.759 / 1.759 OK` (100,00%, `0 FAIL`, `0 switch(a1)`)** · `p50 = 14,08 ms`, `p95 = 88,18 ms`, `p99 = 177,83 ms`, `max = 2,79 s` (`runtime.initMetrics`) · tổng dòng giảm `137.557 → 131.607` (**−5.950 dòng**) · wall-clock F5 `52,77 s` |
| **`wasm_func_2422` & `wasm_func_1806` (`main.wasm`)** | **`wasm_func_2422` (`0x292ba3`, 284 KB):** `structural` `776,73 s → 0,68 s` (**`1.145×` nhanh hơn**), `total` `840,58 s → 54,86 s` (**`< 60 s` ✅, `15,3×`**), `goto` `1.005 → 0` (**−100%**) · **`wasm_func_1806` (`0x1669be`, 334 KB):** `structural` `576,05 s → 1,38 s` (**`418×` nhanh hơn**), `total` `686,20 s → 93,59–110,39 s` (**`6,2–7,3×`**), `goto` `1.007 → 0` (**−100%**) |

**Bảng phân loại thay đổi (rev12 — G6 `core/toolchains/go/ir.{hpp,cpp}`, `decompiler/filter.{hpp,cpp}`):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | Đổi hợp lệ lớp `cold-tail-severed` | Số `goto` (Trước → **Sau**) | Ghi chú |
|---|---:|---:|---:|---:|---|
| `main.wasm` (`first100`) | 100 | **93 / 100** (`93 OK`) | **7 / 100** (`wasm_func_25, 26, 27, 29, 33, 90, 110`) | `62` → **`33`** (**−29**, **−46,8%**) | 5 / 7 hàm đổi giảm về `0 goto` (`wasm_func_25, 26, 29, 90, 110`) |
| `main.wasm` (`wasm_func_2422` & `wasm_func_1806`) | 2 | 0 | **2 / 2** (`2422`: `20.007` dòng; `1806`: `25.297` dòng) | `2.012` (`1.005 + 1.007`) → **`0`** (**−100%**) | `structural`: `776,73 s → 0,68 s` (`2422`) & `576,05 s → 1,38 s` (`1806`) |
| `gowasm-balls` (5 hàm smoke) | 5 | **5 / 5** (`5 OK`) | 0 | `0` → **`0`** | Bất biến 100% (`a73e49e5…`, `456` dòng) |
| `gowasm-balls` (toàn module) | 1.759 | **1.759 / 1.759 `OK`** (`0 FAIL`) | `913` cold-tail blocks được cắt chuỗi rơi thông | `total_gotos = 989` (`total_labels = 682`) | Tổng dòng giảm `137.557 → 131.607` (**−5.950 dòng**) |
| `tests/golden/` (12 Go fixtures) | 52 | **52 / 52** (`52 PASS`) | 0 | — | Bất biến 100% (`0` repair) |
| `duckdb.wasm` (`stride=100`) | 634 | **634 / 634** (`634 OK`) | 0 | — | F5-subset bất biến 100% (`5f728cb0…`) cả `hidden` và `explicit` |

**Cơ chế & dẫn chứng cho lớp `cold-tail-severed`:**
1. **Điều kiện chứng minh hàm đích terminal noreturn thuần cấu trúc (`is_go_terminal_noreturn_function` trong `core/toolchains/go/ir.cpp`):**
   - Một hàm nội bộ trong module Go Wasm hiện đại được chứng minh là terminal noreturn khi và chỉ khi IR của chính nó thỏa mãn đủ 4 điều kiện cấu trúc: (i) `ir.present && ir.unwind_exit != UINT32_MAX && ir.pc_b_register_proof`; (ii) `ir.ret.empty()` — **0** lệnh `return` (`0x0F`, bởi mọi hàm Go Wasm muốn trả điều khiển bình thường về caller đều bắt buộc đi qua lệnh `return` với `i32.const 0`); (iii) không có cạnh `go_jmp_ir_t` nhảy lùi (`target_offset <= br_offset`); (iv) không phải `runtime.morestack` / `runtime.morestack_noctxt` (chứng minh qua tên hoặc mẫu 20 byte của wrapper `morestack_noctxt` trên module bị strip).
   - Lưu cờ `is_noreturn_call` (bit `4` của `pcf_flags` trong tail `GOPC` v1 thuộc blob `'x'` v3, tương thích ngược hoàn toàn).
2. **Đóng khối cold-tail tại `br_if $unwindExit` (`decompiler/filter.cpp:1385`) và làm phẳng chuỗi `type == 1` (`sequence`) tại `hxe_structural`:**
   - Khi `apply()` tiêu thụ lệnh `br_if $unwindExit` ngay sau lời gọi tới hàm `is_noreturn_call` và khung điều khiển trên đỉnh `controls` là một khối `block` (`controls.back().kind == 0x02`, tức nhánh resume `$bK` ở đuôi hàm, phân biệt với guard `if (p == 0) sigpanic()` nội tuyến có `controls.back().kind == 0x04`), phát thêm `emit_unreachable(cdg)` (`__builtin_trap()` với `FCI_NORET`) để đóng khối thành nút lá độc lập thay vì rơi thông sang khối cold-tail kế tiếp.
   - Trên các hàm có `> 1.000` câu lệnh `if` nối tiếp dọc trục chính (như `wasm_func_2422` với `1.444` mức và `wasm_func_1806` với `1.923` mức sau khi 993/998 khối cold-tail gập thành các khối `if` độc lập), bộ rút gọn cấu trúc của Hex-Rays tạo cây nhị phân lệch phải gồm các nút vùng `type == 1` (`sequence`) `[if_1, [if_2, ...]]`; callback `go_structural_hexrays_callback` tại `hxe_structural` làm phẳng các nút con `type == 1` trực tiếp vào nút cha `type == 1` khi độ sâu cây vùng `>= 512`, giữ nguyên `cblock_t` đầu ra nhưng tránh vượt ngưỡng đệ quy `1.024` (`0x400`) của `cgen_t::emit_node`.
3. **Dẫn chứng cụ thể trên `wasm_func_90` (`0x5741`, `goto: 2 → 0`, xóa chuỗi rơi thông `LABEL_42 → LABEL_43` và 2 phép khởi tạo biến giả `v1 = 0; v2 = 0;`):**
   ```diff
   -      if ( 2 * i >= v9 )
   -        goto LABEL_43;
   +      if ( 2 * i >= v7 )
   +      {
   +        ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>))wasm_func_674)(v9, v7);
   +        __builtin_trap();
   +      }
          ...
          if ( v14 == 0 )
   -        goto LABEL_42;
   +      {
   +        ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>))wasm_func_674)(v12, v7);
   +        __builtin_trap();
   +      }
          ...
        if ( v7 == 0 )
        {
          ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>))wasm_func_674)(0, 0);
   -LABEL_42:
   -      ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>))wasm_func_674)(v2, v9);
   -LABEL_43:
   -      ((void (__usercall *)(__int64@<r0:r1>, __int64@<r2:r3>))wasm_func_674)(v1, v9);
          __builtin_trap();
        }
   ```

### T0 rev13 — C5 vùng EH có cấu trúc: chú thích ranh giới `try-region` / `EH landing pad` (c1) + thu gọn nguyên tử landing pad rỗng thành `__cxa_rethrow()` (c2)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `3610dadb6531eecc8eebe244fca5bb1d` (`WASMHEX_BUILD_MARKER_C5_EH_REGIONS_COLLAPSE`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `d86048c5d0b745a3a67a36cb208c5af9` (`WASMHEX_BUILD_MARKER_G6_PANIC_NORETURN_COLD_TAIL`) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev13, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **`T0_SWEEP_DONE ok=100 fail=0`** (**100 / 100 hàm byte-identical 100%** so với rev12; C5 chỉ tác động tới giao thức `__THREW__` / `__cxa_find_matching_catch_*` của C++) |
| main100 `sha256` pseudocode-only (`awk '/^T0_SWEEP total=/,/^T0_SWEEP_DONE/'`) | `8918d4c9a5e653b8cdd67233859eaa1816748f046b28939c543e39b4131dd7e9` (**BẤT BIẾN 100/100 hàm byte-identical so với rev12**) |
| gowasm-balls 5-hàm smoke `sha256` log đầy đủ (rev13) | `acce0706e5cf661a273b49cc128cf5543f4c50f1077e4c4aa682ada915bdf1b8` (**BẤT BIẾN 100% so với rev9–rev12**) |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev13) | `a73e49e5be42f6da95923d7ef18d183202798579149273f3849d15fd7b08c6d1` (**BẤT BIẾN 5/5 hàm byte-identical so với rev9–rev12**) |
| `tests/integration/golden/mergeProcessed_baseline.txt` | **1 / 1** (`4` chú thích `// wasmhex: try-region` tại các lời gọi được bảo vệ + `2 / 2` chú thích `// wasmhex: EH landing pad (catch + cleanup + resume)` tại `LABEL_60` và `LABEL_62`; giữ nguyên toàn bộ 6 chỉ số `lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68`, `0` orphan comments) |

**Bảng phân loại thay đổi (rev13 — C5 `native/wasmhex/decompiler/rewriter_eh.cpp`):**

| Corpus / tập mẫu | Tổng hàm | Không đổi (`OK` byte-identical) | Đổi hợp lệ do C5 (`try-region` + `landing pad` + `c2 collapse`) | Ghi chú |
|---|---:|---:|---:|---|
| `main.wasm` (`first100`) | 100 | **100 / 100** (`100 OK`) | 0 | Bất biến 100% (Go không bị chạm) |
| `gowasm-balls` (5 hàm smoke) | 5 | **5 / 5** (`5 OK`) | 0 | Bất biến 100% (`a73e49e5…`, Go không bị chạm) |
| `tests/golden/` (12 Go fixtures + 9 C/C++/DWARF fixtures) | 92 | **92 / 92** (`52 + 40 PASS`) | 0 | Bất biến 100% (`0` repair) |
| `tests/integration/golden/mergeProcessed_baseline.txt` | 1 | — | **1 / 1** (`try_cmts: 0 → 4`, `pad_cmts: 1 → 2`, `lines=265`, `goto=8`, `lvars=68`, `orphans=0`) | C5.c1 đánh dấu đủ 4 lời gọi try-region và cả 2 landing pad (`LABEL_60`, `LABEL_62`) |
| `duckdb.wasm` (`stride=100` + các hàm C++ EH) | 634 | **634 / 634** (`634 OK`) | C5.c1 đánh dấu ranh giới `try-region` & mọi landing pad (kể cả chuỗi `goto` cleanup chung); C5.c2 thu gọn nguyên tử các landing pad rỗng thành `__cxa_rethrow()` | F5-subset bất biến 100% (`5f728cb0…`) cả `hidden` và `explicit` |

**Cơ chế & dẫn chứng cho C5 (`rewriter_eh.cpp`):**
1. **C5.c1 — Phân loại landing pad qua chuỗi `goto` cleanup chung và chú thích ranh giới `// wasmhex: try-region` (`is_valid_eh_landing_pad` / `structure_eh_regions`):**
   - Lần theo mọi câu lệnh `v_exc = __cxa_find_matching_catch_*()` qua các câu lệnh cleanup (lời gọi hàm/phép gán) và các cạnh `goto` không điều kiện nối vào chuỗi cleanup chung cho tới `__resumeException(v_exc); __builtin_trap();`. Nhờ đó, số khối landing pad được nhận diện và gắn `// wasmhex: EH landing pad (catch + cleanup + resume)` tăng từ `1 → 39` trên `ExtractJoinRelations` (`41` tổng cộng trừ `2` khối nội bộ trong `C6.4 vector::push_back`), `2 → 3` trên `AddRelationTdom`, `3 → 4` trên `ExecuteTask`, và `1 → 2` trên `mergeProcessed`.
   - Gắn chú thích `// wasmhex: try-region` (`ITP_SEMI`) trực tiếp lên câu lệnh gọi hàm được bảo vệ ngay trước mỗi kiểm tra `if (*__THREW__ == 1)` / `!= 1` (`ExtractJoinRelations`: `155` site; `AddRelationTdom`: `3` site; `ExecuteTask`: `5` site; `mergeProcessed`: `4` site; **`0` orphan comments** trên tất cả các hàm).
2. **C5.c2 — Thu gọn nguyên tử landing pad có cleanup rỗng thành `__cxa_rethrow()` (`classify_eh_pad_collapse` / `verify_eh_empty_pad_collapse`):**
   - Khi và chỉ khi một khối landing pad thỏa mãn đủ 5 điều kiện chứng minh: (i) bắt đầu bằng catch-all `v_exc = __cxa_find_matching_catch_2()`, (ii) kết thúc ngay bằng `__resumeException(v_exc); __builtin_trap();` với `cleanup_stmt_count == 0`, (iii) mọi cạnh đi vào pad (trực tiếp hoặc qua nhãn `goto LABEL_k`) đều là guard chính xác `if (*(int *)GOT.mem.__THREW__ == 1)` (`non_guard_predecessor_count == 0`), (iv) mọi guard đều nằm liền kề ngay sau lời gọi invoke tương ứng (`all_guards_adjacent_to_invoke`), và (v) biến `v_exc` chỉ có đúng 1 lần ghi và 1 lần đọc trong toàn hàm (`catch_temp_does_not_escape`), trình viết lại thu gọn nguyên tử toàn bộ các guard và khối pad thành `if (*(int *)GOT.mem.__THREW__ == 1) __cxa_rethrow(); // wasmhex: try-region rethrow (empty landing pad collapsed)`, xóa khối pad + nhãn không còn dùng và ẩn khai báo biến tạm `matching_catch_2` (`clear_used()`).
   - Dẫn chứng cụ thể trên `std::__fs::filesystem::__permissions` (`duckdb.wasm` @ `0x1845875`, 2 guard cùng nhảy vào 1 landing pad rỗng tại `LABEL_17`, giảm `110 → 104` dòng = **−6 dòng**, xóa `LABEL_17` và biến `matching_catch_2`):
     ```diff
     -    std::__2::__fs::filesystem::detail::ErrorHandler<void>::report((int)v7, (int)v18);
     -    if ( *(int *)GOT.mem.__THREW__ == 1 )
     -    {
     -LABEL_17:
     -      matching_catch_2 = __cxa_find_matching_catch_2();// wasmhex: EH landing pad (catch + cleanup + resume)
     -      __resumeException(matching_catch_2);
     -      __builtin_trap();
     -    }
     +    std::__2::__fs::filesystem::detail::ErrorHandler<void>::report((int)v7, (int)v18);
     +    if ( *(int *)GOT.mem.__THREW__ == 1 )
     +      __cxa_rethrow();                          // wasmhex: try-region rethrow (empty landing pad collapsed)
        }
        else if ( v13 != 0 )
        {
          v23 = HIDWORD(v35);
          std::__2::__fs::filesystem::operator|=[abi:ne180100]((int)v40, v23);
          if ( *(int *)GOT.mem.__THREW__ == 1 )
     -      goto LABEL_17;
     +      __cxa_rethrow();                          // wasmhex: try-region rethrow (empty landing pad collapsed)
        }
     ```

### T0 rev14 — G6 khép phạm vi display: khảo cứu runtime wrappers (wasmDiv/memeqbody/memcmp/memchr/wasmexport) + gate structural sigpanic-guard cho module strip (wb-collapse `wasm_func_1806` `43 → 250/250`)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `68369b3b8b792a6e610b6eea44f0c78c` (`WASMHEX_BUILD_MARKER_G6_WB_STRUCTURAL_GUARD` mới; giữ `…_WRITE_BARRIER_COLLAPSE`, `…_WB_NIL_GUARD_COLLAPSE`, `…_PANIC_NORETURN_COLD_TAIL`) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `41032c9d0852b6bcd6535333bd0a19c1` (build lại từ tree hiện tại sau khi WIP loader khác bị thay giữa phiên; cặp procs+loaders PHẢI cùng tree — loader build từ ir.cpp bản khác sẽ ghi blob không tương thích và làm mất cả G3 lẫn gate structural trên fixture) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev14, `634/634 OK` cả `hidden` và `explicit`)** |
| main100 kết quả | **99 / 100 byte-identical + 1 hàm đổi hợp lệ** (`wasm_func_43`: wb-collapse do gate structural — site từng skip `extra_side_effect_in_body` ở rev9 nay khớp; `39 → 25` dòng); tổng dòng `5.981 → 5.967` (−14) |
| gowasm-balls toàn module (1.759 hàm) | **`1.759 / 1.759 OK`, pseudocode byte-identical 100%** so với trước gate (dump-golden 2 chiều, `0` file khác; `660` comment `go: write barrier` giữ nguyên — gate chỉ mở cho callee tên tổng hợp `wasm_func_N`, balls có tên pclntab) |
| gowasm-balls 5-hàm smoke `sha256` pseudocode-only (rev14) | `8c3b4c5707352d6d3914e2c0339cd4c10c429f309cdf7f8ae257c1d4486a4ea0` (5/5 hàm byte-identical so với trước gate) |
| `main.wasm` diff toàn module (3.255 hàm) | **9 hàm đổi / 3.255 — tất cả thuộc loại `go-runtime-display` (wb-collapse)**: `43` (0→1 cmt, 39→25 dòng), `367` (4→5), `1015` (0→1), `1134` (4→5), `1301` (2→3, 81→65), `1806` (**43→250 = 250/250 site**, 25.301→21.563 = **−3.738 dòng**), `2154` (0→3), `2357` (2→3), `2422` (122→123); tổng comment `1.065 → 1.282` (+217) |
| `tests/golden/` + fixture mới | `expect` **46/46** (45 cũ + `g6_wb_structural_guard` 5/5: positive collapse + near-miss loop-helper fail-closed + refused-helper marker); `core_tests` **70/70** trên tree commit này (69 gốc + test mới "G6: structural sigpanic-guard gate"; `spec_oracle` hai ngưỡng đếm `checked 403→404` cập nhật theo tsv; các test G5 khác đang nằm trong stash WIP của worker song song, không thuộc commit này) |

**Cơ chế & dẫn chứng (rewriter_go.cpp — không đụng ir/filter/lower):**
1. **Khảo cứu display runtime wrappers (todo §5.1 `notUsePC_B`, kết luận "đã đọc được"):** `runtime.wasmDiv` (main/balls) hiển thị guard tràn `MinInt64 / -1` + `a / b`; `memeqbody`/`memcmp`/`memchr`/`cmpbody` hiển thị vòng so sánh byte; `wasm_export_run/resume/getsp` hiển thị `SP = hằng; store×2; rt0_go(0); wasm_pc_f_loop()`. **`wasmTruncS/U` không tồn tại** trong cả 2 corpus (0 lệnh `i64.trunc_f64_s`; chỉ `i64.trunc_sat_*`); kiểm `SP == 0 → notInitialized` của master không có trong go1.26.5/1.27.3 → không land gì không có bằng chứng.
2. **Gate structural sigpanic-guard:** `is_sigpanic_callee` mở rộng — callee `wasm_func_N` + mọi đối số hằng 0 + `load_go_function_ir` (netnode `'x'`, metadata.hpp sẵn có) + `loader::is_go_terminal_noreturn_function(ir, false)` = CHÍNH vị từ của `93afba1` (IR hợp lệ · `unwind_exit` · `pc_b_register_proof` · `ir.ret` rỗng · không JMP lùi). Bằng chứng đích: `wasm_func_671` (main.wasm) có body dispatcher trùng khớp `runtime.sigpanic` (balls func[648]) và 0 lệnh `return` (unit test pin). Guard lặp sau khối `if` giữ nguyên → hiển thị còn đúng 1 lần nil-check trước store (hợp đồng `docs/SEMANTICS.md` §5d mới).
3. **Fixture `g6_wb_structural_guard.wasm`** (strip hoàn toàn — không pclntab): wb callee chứng minh bằng bytecode 2 tầng (wrapper 5 lệnh + core vòng `wbBuf` ≥60 B), guard helper terminal (forward JMP) → collapse; near-miss loop-helper (backward JMP) → fail-closed `extra_side_effect_in_body`, block giữ nguyên (expect `regex: wasm_func_2\(\)` + `contains: go: write barrier` phân biệt 2 đường).

### T0 rev15 — G5-S1 frame evidence (display-neutral) + G5-S2a hàm Go void trả về `void` (1.616 hàm đổi hợp lệ `go-result-void`)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 (rev15-note S1) | `6cb7e1d2877bc5aa7972e25534c914f2` (`WASMHEX_BUILD_MARKER_G5_FRAME_EVIDENCE` mới; S1 display-neutral — balls F5-subset `36d4127a…` byte-identical 3 trạng thái) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 (rev15 S2a) | `18f676acb0663a0cd73a792478945e2e` (`WASMHEX_BUILD_MARKER_G5_VOID_RESULTS` mới trong loader; procs giữ `6cb7e1d2…` — S2a chỉ đổi loader-side prototype/`'5'`) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev15, `634/634 OK` cả `hidden` và `explicit`)** |
| gowasm-balls toàn module (1.759 hàm) | **`1.759 / 1.759 OK`**; machine-categorized (dump-golden 2 chiều, 1.757 file `.c`): identical 960 · `go-result-void` mechanical 783 · `go-result-void-restructured` 8 (hexrays dàn CFG lại khi mất return-reg) · `go-result-void-caller-site` 6 (chỉ hiện synthetic cast vì callee void-hóa; mọi callee ∈ void-set) — **0 thay đổi ngoài lớp S2a**; 783+8 = 791 = 792 hàm proven-void (pin core_tests) − 1 refused |
| `main.wasm` toàn module (3.255 hàm) | `3.255 / 3.255 OK`; identical 2.426 · mechanical 815 · restructured 9 · caller-site 5 — 815+9 = **824 = đúng pin core_tests**; main100 (100 hàm đầu) `100/100 OK` |
| `tests/golden/` | **KHÔNG rebaseline** — `expect` 46/46 giữ nguyên (fixtures Go synthetic không đạt frame-proven); golden-merge strict OK |
| Cơ chế | `go_proven_void_results` (loader/database.cpp) đọc GOFR tail (đã build trước vòng prototype vì `build_go_function_irs` kết thúc `module_t::parse()`): frame_valid + `results` rỗng + có `ret` → entry.cpp hiển thị `void f(...)`: `'5'`/`'7'` lưu vector rỗng, mọi prototype builder nhận `display`; `'P'/'Q'` giữ nguyên RETUNWIND i32 cho call-site. RETUNWIND giữ nguyên trong thân: `i32.const 0` trước `return` thành dead push (nhánh void sẵn có của `emit_return`), tail unwind `i32.const 1; end` bị chặn bởi `unreachable` như mọi hàm `()->()` |
| Bar khác | `core_tests` **72/72** (bar mới +2 test G5) · fuzz **45.000.050 exec no-crash** (2 cây: S1-clean + S2a patch, qua worktree vì WIP worker song hành làm hỏng build fuzz cây chính) · format/tidy/dep: file G5 sạch (format-check cây chính fail duy nhất ở `test_go_types.cpp` — WIP untracked của worker khác, không thuộc commit này) |

### T0 rev16 — G5-S2b Go results typed (caller-cross-checked, return synthesis)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `736e77f86688b4816bc9757cf1af362f` (`WASMHEX_BUILD_MARKER_G5_RESULT_SYNTHESIS` mới — processor đổi lần đầu từ G5: emit_return tổng hợp load kết quả) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `f7efb0f7b94362b576c5e14207c0e993` (`WASMHEX_BUILD_MARKER_G5_RESULT_TYPES` mới trong loader) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7666fbe79a2df2fbd` (BẤT BIẾN rev1→rev16, `634/634 OK` cả `hidden` và `explicit`)** |
| gowasm-balls toàn module (1.759 hàm) | **`1.759 / 1.759 OK`** (f5 + dump-golden 2 chiều vs baseline HEAD `0d34225` qua worktree); machine-categorized: identical **1.422** · changed **335** — tất cả thuộc duy nhất lớp `go-result-typed` (134 single i64 + 1 single double `math_rand.Float64` + 57 single bool/i32 `{return 0; → return <load>;}` + 143 multi sret) — **0 out-of-class, 0 caller-site-only** |
| main.wasm toàn module (3.255 hàm) | **`3.255 / 3.255 OK`**; identical 2.851 · changed **404 = đúng pin core_tests** (151 i64 + 57 bool/i32 + 196 multi sret) — 0 out-of-class; main100 (100 hàm đầu theo Functions() order) `100/100 OK`, 15/100 đổi hợp lệ (`go-result-typed` duy nhất), 85 byte-identical |
| `tests/golden/` | **KHÔNG rebaseline** — `expect` 46/46, `matrix` 24/24, `check all` 44/44, golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) |
| Cơ chế | (1) `cross_check_go_result_callers` (core ir.cpp, chạy sau vòng per-body trong `build_go_function_irs`): mỗi callee có result records phải được MỌI call site resolved đồng ý — arg window W (max end trên mọi caller, records G4 `go_call_ir_t.args`) ≤ slot kết quả thấp nhất (bắt param write-back qua `param_window` lower-bound), và mọi post-call read `[L + off]` trong result extent phải byte-covered (read ngoài extent = frame local của caller, bỏ qua). Verdict 5 trạng thái (None/Proven/NoCaller/Mismatch/Ambiguous) serialize GOFR v2 (v1 decode → unproven). (2) `go_proven_results` (loader/database.cpp) → entry.cpp display: single scalar qua prototype thường (`__int64 f@<r0:r1>`), multi qua `make_multi_prototype(force_sret)` → `void f(int a1@<r0>, void *result@<r1>)` — tuple bị cấm vì call site giữ RETUNWIND i32 thô ('P'/'Q' nguyên vẹn, hợp đồng `93afba1` bất biến) mà i32 call-result không cast được sang tuple-callee (INTERR 52849 order-dependent, đo thực nghiệm 100 hàm). (3) Filter `reset_state` mirror đúng type sret đó + `emit_return` thay dead-push `i32.const 0` bằng load `[SP_local + framesize + 8 + slot]` (2-form resolver S1; rộng 8 byte qua `m_xdu`+memory base như `make_effective_memory_address`) rồi đẩy vào plumbing return sẵn có (retvar/reg/sret store qua `emit_sret_store` mở rộng wide-DB) |
| Bảng đếm cross-check (pin core_tests) | main.wasm go1.26.5: proven **404** (208 single + 196 multi) · noCaller **1.221** · mismatch **47** (44 window + 3 uncovered read, vd `runtime.pcvalue` pair int32+int64 có padding gap) · ambiguous **0** — tổng 1.672 body có records. gowasm-balls go1.27.3: proven **336** (193 + 143) · noCaller **73** · mismatch **45** (43 window + 2 uncovered read `efaceeq`/`ifaceeq`: caller đọc cả 8-byte slot quanh bool 1 byte) · ambiguous **0** — tổng 454. Void-set S2a KHÔNG đổi (792/824 pin giữ nguyên — không có conflict "void-vs-caller-read" nào chứng minh được vì caller-local frame reads không phân biệt được với result reads khi callee không có store records; đếm 0) |
| Bar khác | `core_tests` **84/84** (+2 test case mới: G5-S2b pins corpora + blob GOFR v2 round-trip + `go_result_slot_types` dedupe/overlap/mapping + ground truth Intn proven/`type_.eq` NoCaller/`main.main.func1` NoCaller) · fuzz **54.000.450 exec no-crash** (standalone ASan+UBSan 120k iter × corpus) · format/tidy/dep clean |

### T0 rev17 — G7-S3 tên kiểu Go qua neo itab/known-API/descriptor + prototype có tên

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `19dfcf361bc4bb2b7e3bd207b1fc2f46` (`WASMHEX_BUILD_MARKER_G7_NAMED_PROTOTYPES` mới — metadata.cpp: ensure_go_struct_type **memo hóa 1 lần/tên** + make_parsed_wasm_function_type_ex + cache riêng per-EA) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `8819ccf24275535e85cdd4cf3d879441` (`WASMHEX_BUILD_MARKER_G7_NAMED_SRET` mới trong loader) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev17, `634/634 OK` cả `hidden` và `explicit` — chạy lại trên binary cuối)** |
| gowasm-balls toàn module (1.759 hàm) | **`1.759 / 1.759 OK`**; machine-categorized (dump-golden 2 chiều vs baseline HEAD `45eb9df`): identical **1.672** · `go-type-comment` **79** (chỉ thêm dòng `// go: <signature> [anchor]`) · `go-type-named` **6** (comment + `void *result` → `go__X *result`) — **0 out-of-class**; nguồn tên: itab **19** · known-API **8** · FuncOf-callback **2** · descriptor-method **56** = **85 hàm có prototype/chữ ký Go có tên** (bar ≥ 24 ✓) |
| gowasm126 (go1.26.5, 1.794 hàm) | `1.794 / 1.794 OK`; itab layer **15 itab** (đúng `itablinks_len` pin S1) → **16** comment `[itab:` + **58** `[method of` — pin `io.Writer → main.devNull` (fn 1811) hiển thị đủ `(main.devNull) Write([]uint8) (int, error)` |
| main.wasm toàn module (3.255 hàm, stripped) | **`3.255 / 3.255 OK — 0 FAIL`** (f5 sweep stride 1 chạy trên binary cuối, F5_SUBSET `4d333ba8…` tái lập); lớp kiểu rỗng (không pclntab — pin S1) → **0 hàm có `// go:`** (grep toàn dump = 0); diff dump 2 chiều HEAD worktree vs bản này: 125 file khác — **toàn bộ là nhiễu register-allocation có sẵn của hexrays trên corpus này** (đối chứng HEAD-vs-HEAD cùng binary cũng lệch cùng cỡ — xem ghi chú dưới), **không có thay đổi ngữ nghĩa** · ghi chú: HEAD runner f5 balls-full tự SIGSEGV ~1500/1759 trong 1 lần chạy (flake có sẵn của môi trường, không thuộc slice này) |
| `tests/golden/` | **KHÔNG rebaseline** — `expect` 46/46, `matrix` 24/24, `check all` 44/44, golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) |
| Cơ chế | (1) `go_itab_layer` (core types.cpp): 1.27 đi vùng `[types+itaboffset, etypes)` consumed-exactly (17 itab [ĐO]); 1.26 qua `itablinks` (15/15); mỗi itab `{inter, type, hash, fun[n]}` với `fun[i] = slot<<16` (cùng encoding funcval PC_F, G4) → `table_slots` → fn; Imethod `{nameoff,typoff}` 8B. (2) `go_resolve_named_signature` — 4 neo theo độ mạnh: itab (chữ ký từ FuncType của method giao diện + receiver từ `itab.type`, đối chiếu ArgInfo byte-exact: params phủ đúng slots contiguous + result bytes khớp khoảng cách `_func.args`) > known-API (bảng tên de-sanitize `syscall_js.Value.*`/`fmt.Sprintf`, fail-closed khi shape lệch) > FuncOf-callback (funcval tĩnh `slot<<16` của body gọi `syscall/js.FuncOf` + shape 40in/16out, chấp nhận khi đúng 1 ứng viên) > descriptor method (ký hiệu `pkg.__T_.m`/`pkg.T.m` + descriptor tồn tại + UncommonType liệt kê method + **Tfn slot trỏ ngược về đúng body**; mtyp gap-cut → chữ ký từng phần `(...)`, không bao giờ đoán arity). (3) entry.cpp: comment repeatable `// go: ...` (Hex-Rays in trên đầu pseudocode) + `go_named_sret_pointee` (database.cpp, pattern go_proven_*: chỉ khi G5-S2b proven multi + đúng 1 result composite có tên + size khớp slot bytes) → `make_multi_prototype(pointee)` render `struct go__X *result`; struct ảo `go__string/go__slice/go__iface/go__<pkg__Name>` tạo trong til từ descriptor (ensure_go_struct_type, metadata.cpp) — cache riêng per-EA (`make_wasm_go_named_function_type`), **không đụng type-cache chung** (bài học C2/M4-S1); blob 't' v1 schema KHÔNG đổi (itabs không serialize — pin round-trip giữ nguyên). (4) Params của hàm Go KHÔNG thể thành tham số C (wasm sig `(i32 PC_B)->(i32)` — filter reset_state khóa arity theo '4') — tên kiểu thật đi qua comment + sret pointee, trung thực với machinery |
| Pin dump (3 hàm) | `fmt.Sprintf`: `// go: fmt.Sprintf(string, []any...) string [known-API]` + `void __usercall fmt.Sprintf(int a1@<r0>, go__string *result@<r1>)` · `main.main.func1`: `// go: func(js.Value, []js.Value) any [js.FuncOf callback passed at main.main]` · `errors.__errorString_.Error`: `// go: error.Error(*errors.errorString) string [itab: error -> *errors.errorString]` |
| Bar khác | `core_tests` **89/89** (+6 test case G7-S3: itab layer 2 corpus + named signatures 4 neo + loader-order replication + C-struct go__js__Value/go__main__Ball + blob 't' untouched) · fuzz **45.000.450 exec no-crash** × 2 vòng (standalone ASan+UBSan 100k iter × 450 corpus, chạy lại trên nguồn cuối) · format-check clean · tidy-check clean (types.cpp chưa trong tidy list — G7-S2 thêm, không đụng CMakeLists) · dep-check OK · strings marker cả 2 dylib · sweep balls `36d4127a…`/main `4d333ba8…` F5_SUBSET tái lập deterministic |
| 2 bug bắt được chính bởi bar này (ghi lại cho slice sau) | (1) **til churn:** gọi `set_named_type(NTF_REPLACE)` mỗi lần trong vòng load làm renumber ordinal til → hỏng tinfo đã apply của hàm KHÁC — symptom **591 hàm `-31 MERR_ARCH`** (đếm dao động theo ASLR) trên balls f5 sweep; fix: `ensure_go_struct_type` memo hóa theo tên (chỉ tạo 1 lần, skip khi `get_named_type` đã đúng size) — f5 về `1.759/1.759 failures=0` ngay. (2) **default `complete=false`** lọt vào khi thêm field `receiver` — mọi chữ ký render từng phần `(...)`; bắt bởi test loader-order replication + pin `error.Error(...)`. Ngoài ra sửa **UAF tiềm ẩn**: `resolve_go_type_addr` append vào `descriptors` → mọi `const go_type_descriptor_t*` giữ qua lần resolve kế tiếp đổi thành giữ-by-value |

#### Ghi chú trung thực rev17 — nhiễu dump main.wasm

main.wasm (stripped, không pclntab) không có lớp kiểu → đường code mới trong loader không
chạy (`module.go_types.present == false`, pin core_tests). f5 sweep stride 1 `3.255/3.255 OK`.
Diff dump-golden 2 chiều (HEAD worktree `45eb9df` vs rev17, mỗi bên IDAUSR riêng) ra **125
file .c khác** ở các hàm chứa store trả về `MEMORY[0x100A00000] = 428874128;` (return-address
push PC_F-encoded) — dạng khác biệt: biến krXX_8 bị đổi, spill bị gộp/mở, dòng MEMORY[...]
hiện/ẩn. **Đối chứng HEAD-vs-HEAD (cùng binary HEAD dump 2 lần) cho cùng loại nhiễu cùng cỡ**
— hexrays register allocation nhạy theo thứ tự phiên/cấu trúc DB tạm (mkdtemp) trên corpus
này; rev15/16 từng ghi nhận "order-dependent" INTERR cùng gốc. gowasm-balls (2 chiều qua 2
binary khác nhau) ổn định: 85 thay đổi tất cả trong 2 lớp cơ khí — noise trên main.wasm
không xuất hiện trên balls. Kết luận ghi thẳng: **rev17 không gây thay đổi ngữ nghĩa nào trên
main.wasm; diff 125 file là nhiễu render có sẵn, được đối chứng tách khỏi thay đổi của em.**
PLACEHOLDER_SPLIT
### T0 rev18 — G7-S2 Go aggregate args tại call-site (ArgInfo-shaped, anchored khi chứng minh được)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `455066226c5d308960f1be97ec32ac12` (`WASMHEX_BUILD_MARKER_G7_ARG_AGGREGATES` mới; đo với blob 't' từ loader patch đo cục bộ — commit loader write thuộc G7-S3) |
| **F5-subset duckdb hidden (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev18)** |
| gowasm-balls toàn module (1.759 hàm) | **`1.759 / 1.759 OK`**, machine-categorized: identical **1.213** · `go-arg-aggregate` **545** (77 group `go_slice *` 3-từ + 351 group 2-từ + pointer-args 415) · `til-side-data-naming` 1 (`os.init.0.c`: `qword_X[k]` → `unk_X+off` — việc đăng ký struct go_* trong til đổi cách IDA tự đặt tên data item) — **0 out-of-class** |
| main.wasm (strip go1.26.5) toàn module | **`3.255 / 3.255 OK`· diff vs baseline HEAD `45eb9df` = `0 / 3.255` file đổi** (không pclntab → không blob 't' → display-neutral đúng thiết kế) |
| gowasm126 (go1.26.5) | `1.794 / 1.794 OK`, 128 hàm có merge (`main.Pt.String` → `fmt.Sprintf(v2 + 0x100000000LL, (go_slice *)(v2 + 0x100000010LL))`) |
| Bar pin (balls) | `main.main.func1` gọi `main.loop(v2 + 0x100000000LL)` — **1 đối số** thay 3 từ rời (3 store window hiển thị trung thực phía trên) · `fmt.Sprintf` = 2 pointer args (string 16B + `go_slice *`) thay 5 từ · `main.main.func1` shape `(js.Value iface 2-từ, []js.Value slice 3-từ)` pin unit test |
| `tests/golden/` | **KHÔNG rebaseline** — expect **46/46**, matrix 24/24, check 44/44, golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) — fixtures Go synthetic không có ArgInfo thật → display-neutral đúng dự đoán |
| Bar khác | core_tests **85/85** (72.078 asserts, +2 test G7-S2 pin group boundaries Sprintf/loop/func1 + round-trip bit24) · fuzz standalone **54.000.450 exec no-crash** (bar ≥100k) · format-check sạch · tidy-check sạch (`core/toolchains/go/types.cpp` vào tidy list) · dep_check OK |
| Cơ chế | (1) ArgInfo decode ghi ranh giới top-level aggregate (StartAgg/EndAgg 0xfe/0xfd) vào `group_starts` song song slots — blob 't' v1 giữ nguyên word-count, bit24 của word packed chở flag (chưa từng có writer); (2) filter `collect_go_call_shapes` đọc blob 't' (module-level, key 0, tag 't'), import-count = 'F' key nhỏ nhất, walk cửa sổ outgoing-args từ anchor SP-=8 xuống từng store (form A quintet `local.get 1; extend; const K; add; wrap` memarg 0 / form B bare `local.get 1` memarg K; value SP+k prefix 4/5 lệnh được skip), đối chiếu slots ArgInfo byte-exact + gate stack-neutral (mọi store ghi-record phải nằm trong window, mọi scalar window store phải được record) + neo descriptor func-type đồng thuận (kind 24→`go_string`, 20→`go_iface`, extent 24→`go_slice`, 16B không đồng thuận→`go_agg16`, group >24B bỏ merge fail-closed); (3) builder `__usercall`: mỗi group = MỘT tham số `TYPE *a_i@<rN:rN+1>` (pointer 8 byte — cùng kỷ luật sret `void *result`), giá trị tổng hợp `xdu(SP_local) + (memory_base + group_offset)`; các từ của group render là store window thật (visible), scalar giữ word-arg như cũ |
| Giới hạn trung thực | tham số by-value C không thể biểu diễn (reg ABI 4/8 byte — `parse_decl` từ chối mọi dạng struct arg; bằng chứng thí nghiệm trong commit G7-S3 todo) → aggregate = pointer tới window thật + store visible; địa chỉ hiển thị `v2 + 0x100000000LL` (chưa fold được thành `&byte_100000000[v2]` — follow-up) |

### T0 rev19 — G7-S2 activation: loader write blob 't' (display aggregate sống)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `504ef74f971d9812a42ad28277d60ae8` (giữ `WASMHEX_BUILD_MARKER_G7_ARG_AGGREGATES`; rebuild chung tree sau tidy-fix 1 dòng `reserve` trong types.cpp — fallback merge, xem dưới) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `545f64e9422e1c0380fcab5ca22b2ef1` (ĐỔI từ rev17 `8819ccf2…` — lần ĐẦU loader mang blob-'t' write; marker mới `WASMHEX_BUILD_MARKER_G7_T_BLOB_WIRED` xác nhận strings) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev19, `634/634 OK` cả `hidden` và `explicit`)** |
| gowasm-balls toàn module (1.759 hàm) | **`1.759 / 1.759 OK`**; machine-categorized (dump-golden 2 chiều, baseline = HEAD `91507ea` loader không-blob): identical **1.211** · `go-arg-aggregate` **545** · `til-side-data-naming` **1** (`os.init.0.c` `qword_X[k]` → `unk_X+off`) — **0 out-of-class**; 428 aggregate pointer group (71 `go_slice *` + 357 group 2-từ theo quy tắc đếm diff-line của rev này; tổng 428 = 77+351 của rev18) |
| Bar pin (balls) | `main.main.func1` → `main.loop(v2 + 0x100000000LL)` — **1 đối số**, 3 window store visible phía trên · `main.__Ball_.draw` → `fmt.Sprintf(v2 + 0x100000000LL, (go__string *)(v2 + 0x100000010LL))` — **2 pointer tham số** trên 5 window store visible — đúng từng chữ pin rev18 |
| main.wasm (strip go1.26.5) toàn module | **`3.255 / 3.255 OK` · diff = `0 / 3.255` hàm đổi** (chỉ `_metrics.json` timing noise) — không pclntab → `go_types.present=false` → blob không viết → display-neutral đúng thiết kế |
| gowasm126 (go1.26.5, 1.794 hàm) | `1.794 / 1.794 OK` sweep; **REGRESSION phát hiện & cô lập (của merge `91507ea`, không thuộc wire rev này)**: vòng named-signature G7-S3 trong entry.cpp tắt display aggregate trên corpus 1.26 — ma trận 2×2 (procs/loader × S2-tree/master): loader-S2 → **128 merge đúng pin rev18** (`main.Pt.String` → `fmt.Sprintf(v2 + 0x100000000LL, (go_slice *)(v2 + 0x100000010LL))`); loader-master → **0 merge**; diagnostic loader-master bỏ vòng named-sig → **128 merge trở lại đúng số**. balls (1.27) hai lớp cộng tồn (545 merge + 85 named) — lỗi tương tác chỉ trên 1.26, ownership G7-S3 (entry.cpp loop × types.cpp/metadata.cpp) |
| `tests/golden/` | **KHÔNG rebaseline** — expect **46/46**, matrix **24/24**, check all **44/44**, golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) — fixtures Go synthetic không có ArgInfo thật → display-neutral đúng dự đoán |
| Bar khác | core_tests **90/90** clean-first (72.152 asserts, bar 85+) · fuzz standalone **55.560.463 exec no-crash** + xác nhận cây-cuối **139.363 exec no-crash** · format-check sạch · tidy-check sạch sau 1 fix merge-fallout (`types.cpp` `raw.reserve` — G7-S2 đưa types.cpp vào tidy list, G7-S3 thêm loop, merge chưa chạy tidy) · dep_check OK |
| Cơ chế | entry.cpp: viết blob 't' MỘT lần lúc load khi `module.go_types.present` (sau `store_go_module`, trước mọi store per-function) — `encode_go_type_info_blob` codec v1 có sẵn, `setblob(words, bytes, 0, 't')`; `delblob(0,'t')` trong block hygiene reload (đối xứng supdel 'm'/'n'/'Y'/'w'/'L') để reload module khác không bỏ lại ArgInfo stale |

### T0 rev20 — G7 1.26 named-signature × aggregate layers coexist (khép phát hiện rev19)

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `71711fd827cbcf51a4368dc7f9e0cab9` (ĐỔI từ rev19 `504ef74f…` — memoize `ensure_go_aggregate_type`; 18 marker strings giữ đủ) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `545f64e9422e1c0380fcab5ca22b2ef1` (**GIỮ NGUYÊN** rev19 — không đổi nguồn loader; marker `WASMHEX_BUILD_MARKER_G7_T_BLOB_WIRED` strings xác nhận) |
| **Ma trận tái lập (máy này, corpus `wasm-test/gowasm126`, rebuild byte-exact HEAD `8068884` = md5 đúng freeze rev19)** | (a) f5 sweep `1.794/1.794` 0 FAIL · F5_SUBSET `604af852…` tái lập deterministic giữa procs rev19 ↔ rev20 · (b) dump-golden ×2 liên tiếp: **byte-identical** (md5-of-md5s `34c1eb97…`), **128 file có merge aggregate + 83 file `// go:` named** — hai lớp CỘNG TỒN · (c) diagnostic build tắt vòng named-sig (loader scratch, procs giữ nguyên): cũng **128 merge**, tập file trùng khớp 100% — **claim "loader-master = 0 merge" của rev19 KHÔNG tái hiện trên cây hiện tại** · (d) delta named-on/off đúng bằng lớp named: 95 file (comment `// go:` + prototype `go__X *result` + cast call-site `(go_slice *)` → `(go__string *)` nơi prototype named của callee phủ kiểu nhóm aggregate — cùng hình dạng pin balls rev19) |
| Bar pin g126 | `main.Pt.String` → `fmt.Sprintf(v2 + 0x100000000LL, (go__string *)(v2 + 0x100000010LL))` — **2 pointer tham số aggregate** trên 5 window store visible · `fmt.Sprintf`: `// go: fmt.Sprintf(string, []any...) string [known-API]` + `void __usercall fmt.Sprintf(int a1@<r0>, go__string *result@<r1>)` · `io.Writer → (main.devNull) Write([]uint8) (int, error)` đủ chữ ký |
| gowasm-balls toàn module (1.759 hàm) | `1.759/1.759 OK` · f5 F5_SUBSET `36d4127a…` tái lập · dump 2 nhánh procs (rev19 `504ef74f…` vs rev20 `71711fd8…`): **byte-identical** (duy nhất `_metrics.json` timing noise) ⇒ mọi số đếm rev19 giữ nguyên bằng đẳng thức: identical 1.211 · `go-arg-aggregate` **545** · `// go:` named **85** (grep 85 file ✓) |
| main.wasm (strip go1.26.5) toàn module | `3.255/3.255 OK` ×2 nhánh · diff base-vs-fix = **0/3.255 file đổi** (không pclntab → cả hai lớp dormant — đúng thiết kế) |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev20, `634/634 OK` cả `hidden` và `explicit`)** |
| `tests/golden/` | **KHÔNG rebaseline** — expect **46/46**, matrix **24/24**, check all **44/44**, golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) |
| Bar khác | core_tests **91/91** clean-first (89.253 asserts; +1 test pin tương tác 1.26 ở loader-level: blob 't' + arginfo/group_starts **bất biến** qua vòng named-signature đầy đủ — itablinks scan + lazy descriptor appends; Sprintf `{0:8,8:8}{16:8,24:8,32:8}` group `[1,0,1,0,0]` args=56; `fmt.Sprintf(string, []any...) string` + `(main.devNull) Write([]uint8) (int, error)` resolve được SAU vòng) · fuzz standalone **67.500.450 exec no-crash** (150k iter × 450 corpus) · format-check sạch · tidy-check sạch · dep_check OK |
| Cơ chế fix | Lớp lỗi thật duy nhất còn lại trên đường tương tác S2×S3: `ensure_go_aggregate_type` (lower_calls.cpp) gọi `set_named_type(NTF_REPLACE)` **không memoize** — mỗi group-param mỗi F5 → churn til-ordinal hàng trăm lần mỗi sweep, đúng lớp bug rev17 từng ghi (`ensure_go_struct_type` memoize, comment metadata.cpp:851-857; symptom `-31 MERR_ARCH` "đếm dao động theo ASLR" = order-dependent). **Fix: memoize theo name+size** (giữ nguyên hành vi khi name chưa tồn tại hoặc size lệch — vẫn REPLACE như cũ); render chứng minh trung tính: dump balls + g126 + strip **byte-identical** procs cũ/mới |
| Kết luận trung thực | "0 merge" của rev19 không tái lập được trên HEAD hiện tại với cùng corpus + binary rebuild byte-exact (ma trận named-on/off trên cùng máy: 128/128). Khả năng cao observation gốc đến từ (i) til-churn order-dependent nay đã cắt khỏi nguồn, hoặc (ii) trạng thái build/deploy của ma trận 2×2 lúc đo (thời điểm cây đang được phục hồi sau accidental checkout — ghi chú types.hpp). Bar rev20 chốt: hai lớp cộng tồn trên cả 1.26 lẫn 1.27, mọi bất biến giữ nguyên |

### T0 rev21 — G7-S4: struct fields, map call-sites, closure anchors + G10 conventions

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `087bec32181dcdef46d6a71b1529209d` (marker mới `WASMHEX_BUILD_MARKER_G7_S4_TYPE_NOTES_AND_NAMED_GROUPS` strings xác nhận; 19 marker strings) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `e39a3734201422d7f0424f03a50b1b89` (ĐỔI từ rev20 `545f64e9…` — loader mang eager closure trong `scan_go_types`; blob `'t'` v1 schema giữ nguyên) |
| gowasm-balls toàn module (1.759 hàm) | `1.759/1.759 OK` · F5_SUBSET `36d4127a…` tái lập đúng freeze rev19/20 · dump ×2 (nhánh deploy-final trước/sau tidy-fix): **byte-identical** trừ `_metrics.json` timing noise (determinism + tidy-neutrality một phép đo) · machine-categorized (dump base=rev20 `71711fd8…` vs S4): identical **1.594** · `go-fields` **19** · `go-map-csite` **11** (3 file makemap + 8 file map-op) · `go-closure` **3** · `go-anchor4` **131** (anchor-4 robust: method table CẢ HAI phía T/*T + break-to-next-pass — thêm `// go:` named cho method value/pointer pair trước đó bị `return false` chặn) — **0 out-of-class** |
| Bar pin balls | `main.loop` + `main.__Ball_.draw`: receiver `syscall_js.Value.Set/Call` hiện `go__js__Value *@<r0>` + cast `(go__js__Value *)` (rule method value-receiver, đúng 1 descriptor kind-25 cùng size — js.Value ref/gcPtr từ descriptor) · makemap: `runtime.initMetrics` `// go: make map[string]runtime.metricData` + `runtime.typelinksinit` `// go: make map[*abi.Type]*abi.Type` + `syscall.map.init.0` `// go: make map[string]syscall.Errno` (bar "≥5" của khảo sát KHÔNG đúng corpus: balls chỉ có 3 call-site makemap nhận descriptor hằng; makemap_small không nhận descriptor — honest skip; bổ sung 14+ call-site họ `runtime.map{assign,access,delete}*` cùng neo descriptor hằng) · closure: `main.main` funcval header store `= 383582208` mang `// go: closure main.main.func1 func(js.Value, []js.Value) any [js.FuncOf callback passed at main.main]` + `syscall.fsCall` (fsCall.func1) + `os.newFile` (os.__file_.close) |
| gowasm126 (1.794 hàm) | `1.794/1.794 OK` · F5_SUBSET `604af852…` tái lập đúng freeze rev20 · categorized: identical **1.625** · `go-fields` **19** · `go-map-csite` **9** · `go-closure` **2** · `go-anchor4` **137** — **0 out-of-class** · pin đối chứng: `main.Pt.String`/`main.__Pt_.String` (go-fields + anchor4) · `main.__devNull_.Write` (anchor4 — `(main.devNull) Write([]uint8) (int, error)` sống lại qua 2-pas method table) |
| main.wasm (strip go1.26.5) toàn module | dump ×2 nhánh (base=rev20 `71711fd8…` qua `~/.idapro` vs S4 deploy-final): **3.255 / 3.255 file identical — 0 / 3.255 hàm đổi** (categorized 0 out-of-class) — không pclntab → blob 't' vắng → mọi lớp S4 dormant đúng thiết kế; f5 sweep `3.255/3.255` 0 FAIL |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev21, `634/634 OK` cả `hidden` và `explicit`, 0 FAIL)** |
| `tests/golden/` | KHÔNG rebaseline — expect **46/46** · matrix **24/24** · golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) |
| Bar khác | core_tests **92/92** clean-first (91 rev20 + 1 test mới pin display API thuần: 16/16 map descriptor render đủ `map[K]V`, `go__js__Value`/`go__main_Ball` c-name, main.Ball 6-field layout x@0(double)..color@32{16B}..size@48 khớp descriptor.size=56, fail-closed addr lạ/kind không phải struct; pins closure 833/831 [ĐO] + closure_added 316) · fuzz standalone **45.000.450 exec no-crash** (100k iter × 450 corpus) · format-check sạch (1 lần chạy format) · tidy-check sạch sau 1 fix `performance-no-automatic-move` (`const std::string name` → non-const trong `go_display_type_name`) · dep_check OK |
| Cơ chế | (1) **Eager reference closure** trong `scan_go_types` (types.cpp, cap 2^15, fail-closed): blob `'t'` v1 schema GIỮ NGUYÊN nhưng mang thêm +316 descriptor mỗi corpus (517→833 balls, 515→831 g126) — named struct (js.Value, main.Ball, main.Pt) có sẵn cho display TRƯỚC vòng named (trước S4 chỉ lazy-resolve SAU khi blob đã viết). (2) **Display API thuần** types.{hpp,cpp} cho decompiler (không module_t): `go_display_type_name`/`go_descriptor_c_name`/`go_descriptor_struct_fields`/`go_descriptor_type_name`. (3) **Named group** (filter.cpp): per-param descriptor agreement (sửa first-group-kind coarseness) + rule value-receiver `pkg.T.m` → `(go__X *)`; til đăng ký qua `ensure_go_struct_type` S3 (memoized) + batch-register ngay collect đầu. (4) **`// go:` notes**: stash per-function → callback hexrays riêng `set_user_cmt` tại CMAT_FINAL (carrier write-barrier; KHÔNG set_cmt giữa microcode — gây MERR_BADARCH -31; không ghi IDB). (5) **Anchor-4 2-pas** (types.cpp): method table T/*T luôn đủ 2 pass, break-to-next-pass thay return false. (6) Khảo sát field names thân hàm: `v->color` KHÔNG render được (địa chỉ giặt qua base segment P0-2 §6) — G10-full cần ctree rewriter, ghi SEMANTICS §6b |
| Bài học root-cause | Mass -31 (-MERR_BADARCH) 17-19 hàm khi bisect KHÔNG phải til churn hay notes — là python global-replace nhầm `k_go_i64_store` (opcode byte 0x37) → `wasmhex_i64_store` (enum 82) ở DÒNG SẴN CÓ của reset_state (so opcode byte) — kiểu bug edit-script im lặng; ghi nhận để lần sau replace có assert count |

### T0 rev22 — ABI audit: `__SET_PAIR__` honest-decline regmap + 2 điểm audit metadata khép

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `01a8a4c973c127fcb1e2be12a9c9a870` (marker mới `WASMHEX_BUILD_MARKER_ABI_AUDIT_8B_RETURN_ENGINE_LIMIT` strings xác nhận; **20 marker strings**) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `349e27e69a76a3970db4c7739570d5f2` (metadata.cpp shared xóa dead-chain — loader rebuild; không đổi hành vi: blob `'t'`/`'x'`/`'4'` schema giữ nguyên) |
| **Probe thực nghiệm mới — `wasmhex_runner abi-probe`** (thêm vào runner, giữ lại làm công cụ tái lập) | (1) **Regmap**: hexx64 gán mreg từ bảng x86 nội bộ — `r0..r7` (toàn bộ thanh ghi ABI wasm, word map {0,1,2,3,6,7}) KHÔNG có entry width-8 (`mreg2reg(m,8)==-1`, `get_mreg_name(m,8)`=-1); `r8..r15` không có mreg; 18 reg có entry 8-byte (`r44..r63` khuyết) trúng vùng MMX/SSE. (2) **E2E cưỡng ép**: `retloc reg1(r0)` + return `__int64` → engine âm thầm pair hóa lệch: prototype `@<r2:r0>` nhưng lvar `// rax` (mâu thuẫn nội bộ); `reg1(r56)` → `result; // mm0` + mất value-tracking; `reg1(r44)` → signature xáo trộn (`int f@<r0>(char a1@<r17>)`). (3) **Callback order**: install A,B → invoke **B,A = LIFO** — cơ sở DECISIONS §27 |
| **`__SET_PAIR__` census (trước = sau, bất biến đúng)** | duckdb pin: `ExtractJoinRelations` (2.411 dòng) `__SET_PAIR__`=0 `__PAIR64__`=0 (LODWORD 4/HIDWORD 2) · `mergeProcessed` (266 dòng) `__SET_PAIR__`=0 `__PAIR64__`=0 (LODWORD 1/HIDWORD 1) — **duckdb SẠCH** · balls (dump 1.759 file): **628 `__SET_PAIR__`**/286 file (17,5%), 47 fed `runtime.gcWriteBarrier{1..3}` + ~54 `wasm_i64_*` (return `__int64` thật), còn lại expression-fed từ pair location — honest-decline DECISIONS §28 |
| Audit 1 (dead-code) | Xóa 4 ký hiệu `make_wasm_go_sret_function_type_named` / `make_wasm_go_named_function_type` / `wasm_go_named_prototype_cache` / `clear_wasm_go_named_prototype_cache` (metadata.cpp + hpp) — grep lần cuối: 0 caller; clear không call-site (rò closebase nếu có consumer) — DECISIONS §26 |
| Audit 2 (libcxx churn) | GIỮ remove/reinstall + comment bằng chứng tại rewriter_libcxx.cpp:1623 — LIFO probe chứng minh dance là cần thiết (libcxx phải install TRƯỚC main ⇒ invoke NGAY SAU main); naive install-once ev_newfile đảo float/Go callbacks lên trước main — DECISIONS §27 (điều kiện mở lại: export 2 callback file-local filter.cpp) |
| gowasm-balls toàn module (1.759 hàm) | `1.759/1.759 OK` failures=0 · F5_SUBSET `36d4127a…` tái lập đúng freeze rev19/20/21 · **dump trước/sau (base=rev21 deploy `087bec32…` vs rev22 `01a8a4c9…`): byte-identical 1.759/1.759 file** (0 khác biệt, trừ `_metrics.json` timing noise) — chứng minh audit xóa/comment trung tính hiển thị |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev22, `634/634 OK` cả `hidden` và `explicit`, `P74_OK max_failures=0`)** |
| `tests/golden/` | KHÔNG rebaseline — expect **46/46** · matrix **24/24** · check all **44/44** · golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) |
| Bar khác | core_tests **92/92** (92.894 asserts) · fuzz standalone **45.000.450 exec no-crash** (100k iter × 450 corpus) · format-check sạch (1 lần chạy format) · tidy-check sạch · dep_check OK · P5 zero-python gate CLEAN

### T0 rev23 — M0-A: 3.1 result-slot cross-check (named-result hạ có đếm) + 3.3 memset/memmove cùng biểu diễn `base + addr`

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `dc59b9930ff26ce71cd199dbff0127f4` (chứa CẢ slice M0-B chạy song song trên cùng cây — 3.4/3.7; md5 ghi trạng thái deploy chung cuối phiên M0-A) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `da185be622053b42f6f2960572b01317` (như trên) |
| Cơ chế 3.1 | `go_sret_result_slot_verdict` (core/toolchains/go/ir.{hpp,cpp}) — phantom sret slot từ raw params × shape-walk ArgInfo `go_arginfo_shape_params` (mirror walk G7-S2) × `min_offset` result-window proven (G5-S2b read-coverage). `kBoundArgument` → `go_named_sret_pointee` tắt + `make_multi_prototype` nhận `result_name=nullptr` → tham số `aN`, giữ `void *`. Loader in 1 dòng đếm `go sret result-slot cross-check: aligned/downgraded (named)`. |
| Cơ chế 3.3 | `emit_memory_bulk` hidden path: dest/src qua `make_effective_memory_address` như scalar (xdu + base trên DB rộng; hằng → `&unk_X`); cơ chế `preset_args` mới của `emit_call` buộc mop thẳng vào call arg; copy parking dest vào operand-register trước transform src; fail-open cho marker/probe-thiếu/hằng-copy; trap hằng ngoài extent như scalar. |
| gowasm-balls toàn module (1.759 hàm) | `1.759/1.759 OK` failures=0 · F5_SUBSET `36d4127a…` tái lập đúng freeze rev19→22 (không đổi — F5-subset chỉ gộp dòng WASMHEX_F5) · machine-categorized (dump base = deploy đầu phiên → deploy cuối, ưu tiên rs-proto > rs-callsite > memset-repr > receiver): identical **1.398** · `go-result-slot-fix` **118** (114 `go__X *result@<rN>` → `void *aN@<rN>` + 4 call-site-only rơi về `go_agg16`/`go_slice` — `syscall_js.Value.Get` v.v.) · `memset-repr` **99 thuần + 10 đan xen** = 109 file chạm (`memset/memmove` dest/src `+0x100000000LL`, hằng → `&unk_X`) · receiver M0-B **142** = slice M0-B song song (không thuộc M0-A) · **0 uncategorized** |
| Pin đối chứng | `fmt.Sprintf`: TRƯỚC `int a1@<r0>, go__string *result@<r1>` + call `(go__string *)(v2+0x100000010)` (slice giả result) → SAU `int a1@<r0>, void *a2@<r1>` + call `(go_slice *)(v2 + 0x100000010)`; `main.__Ball_.draw`: `memset(v2 + 112, 0, 0x50u)` → `memset(v2 + 112 + 0x100000000LL, 0, 0x50u)`. Census unit pin: 336 proven → aligned=0 / bound=272 / neutral=64; multi (denominator loader, 143) → hạ **114** / neutral 29 |
| Fixture mới | `tests/fixtures/m0a_go_result_slot.{wat,wasm}` (G5-S2b proven 2×i64 @40/@48 trên callee synthetic + caller đọc đúng offset; golden + expect) · `tests/fixtures/m0a_memfill_repr.{wat,wasm}` (fill/copy × load/store cùng cửa sổ, động + hằng; golden + expect) · `spec_oracle.tsv` +2 dòng valid |
| **F5-subset duckdb hidden & explicit (DECISIONS #23 — BAR CHÍNH)** | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` (BẤT BIẾN rev1→rev23, `634/634 OK` cả `hidden` và `explicit`, `P74_OK max_failures=0`)** |
| `tests/golden/` | KHÔNG rebaseline fixture cũ nào của M0-A (path C display trùng dạng — `wasmhex_memory_bulk` verify byte-identical; khác biệt a2/g3/g6 trong dump là M0-B/golden stale — để slice sở hữu xử lý). expect **48/48** (46 + 2 mới) · matrix **24/24** · check all **44/44** · golden-merge strict OK (`lines=265 stack=0 helpers=3 coerce=0 goto=8 lvars=68` giữ nguyên) |
| Bar khác | core_tests **94/94** (92.940 asserts; +1 test case M0/3.1 pin census + fixture, +2 dòng tsv) · fuzz standalone **45.200.452 exec no-crash** (100k iter × 452 corpus, chạy trên deploy final) · main.wasm (go1.26.5) f5 **3.255/3.255** failures=0 · format-check sạch (1 lần chạy format) · tidy-check sạch · dep_check OK |

### T0 rev24 — D1 SẢN XUẤT HÓA: linear memory EA 0, `.text`>4GiB, `.globals`<4GiB (RE-BASELINE CÓ CHỦ ĐÍCH)

**Tính chất rev: RE-BASELINE CÓ CHỦ ĐÍCH sau D1** — display đổi cơ học toàn bộ (địa chỉ hiển thị = địa chỉ chương trình).
KHÔNG phải regress; từ rev24 quay lại quy tắc bất biến (F5-subset cố định cho đến thay đổi lowering chủ đích kế tiếp).

| Mục | Giá trị |
|---|---|
| HEAD | merge fast-forward nguyên mẫu `m1-d1` (`bd60591`+`62faefc`, master `0047435` là merge-base trực tiếp — không conflict, giữ nguyên 2 commit nguyên mẫu) + commit sản xuất hóa (điều kiện duyệt a–d) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `441ef02b99567ecbf8d9c8cd9c981931` |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `ff282778ca50fa148da8c241e892f254` (strings marker: procs 22, loaders 14) |
| Cơ chế | `wasmhex_memory_base = 0` (frame.hpp); `.text` tại `align_up(max(4GiB, upper bound))` (`d1_code_base`, internal.hpp); `.globals` dưới 4GiB ngay trên extent đã map + comment rủi ro growable (entry.cpp); bitness MỘT nguồn `memory_mode != 0` đồng bộ loader+processor (DECISIONS #31); 2 cổng >4GiB bỏ: `wasmhex_add_jump` + control-metadata `outside_range` (DECISIONS #32); policy memory64 unbounded: theo max khai báo (kể cả shared/imported/exported/grow), KHÔNG max ⇒ **loader fail-closed có diagnostic** — đóng "honest gap" của nguyên mẫu + chặn silent overflow sentinel `full` (DECISIONS #32) |
| **4 điều kiện duyệt** | (a) `.globals` <4GiB sau extent đã map + comment rủi ro ✓ (entry.cpp); (b) rule bitness một nguồn `memory_mode != 0` — loader tính MỘT lần, processor re-đọc cùng byte 'M' ✓; (c) 2 cổng >4GiB đã bỏ ✓; (d) policy unbounded = theo max khai báo / fail-closed có diagnostic — CHỌN cả hai theo trường hợp, ghi DECISIONS #32 ✓ |
| Probes K5 (giữ trong repo) | `tests/fixtures/m1d1_{memory_globals_brtable,wide_db_constructs,globals_types,no_memory,control_flow_no_memory,memory_nodata}.{wat,wasm}` (6 fixture, spec_oracle +6 dòng) + runner check mới **`m1d1_layout`** (check all 45/45): 32-bit DB + code 0x1000 cho no-memory × 64-bit DB + .text ≥4GiB cho memory-không-data (2 đầu rule bitness, probe pin đúng lớp FATAL cũ); `.globals` end <4GiB + gvar typed F5 xanh; MEMORY@0 disjoint .text>4GiB + display KHÔNG chứa `0x100000000` |
| Hành vi loader đổi có chủ đích (DECISIONS #32) | `wasmhex_fixed_memory64_{full,imported,exported,grow}`, `wasmhex_memory64_v128` giờ **refuse ở loader** với msg rõ (trước-D1: nạp + refuse F5 từng hàm; nguyên mẫu: silent overflow code_base về 0 với `full`); check `fixed_memory64`/`growable_memory64` chuyển 6 fixture sang `expect_loader_rejection` |
| **F5-subset (DECISIONS #23)** | duckdb hidden+explicit **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` BẤT BIẾN rev1→rev24** (634/634 ×2) — display đổi nhưng status-line không đổi: tín hiệu bất biến mạnh hơn dự kiến (re-baseline chỉ cần cho pseudocode hash); balls **`36d4127a…` BẤT BIẾN** (1.759/1.759); g126 **`604af852…` BẤT BIẾN** (1.794/1.794); main full sweep **3.255/3.255** failures=0, F5_SUBSET mới **`4d333ba8ae9284b9d3c0367aef0db3ab5fd07f788a04016105da286bd52e3901`** (anchor mới, chưa có ở rev trước) |
| Machine-categorized balls (base = deploy rev23 `dc59b993…`/`da185be6…`, dump 2 chiều) | 1.759 = identical **71** · `addr-repr-flat` strict **1.472** + reassoc **199** (= 1.671, 99,0%) · `addr-structuring-delta` **15** (0,85% — Hex-Rays structuring đổi theo hình dạng địa chỉ: guard reorder `exithook.Run`, load-fusion `_OWORD` `SetFinalizer`, wb-collapse firing `maps.__Iter_.Next`, loop-condition rotation `memchr`, lvar renumber `mapaccess2` — toàn bộ `runtime.*`/`internal_runtime_*`/`strconv`/`memchr`, đã duyệt từng file, 0 thay đổi ngữ nghĩa) · **0 chưa giải thích** |
| Machine-categorized g126 | 1.794 = identical **75** · flat strict **1.491** + reassoc **207** (= 1.698, 94,6%) · structuring-delta **19** (cùng họ + `internal_strconv.{fixedFtoa,genericFtoa,trimZeros}`, `runtime.findGoroutineLeaks`) · **0 chưa giải thích** |
| `tests/golden/` regen | dump-golden 470 fixture một lần (sạch — lần chạy song song với build genfix bị nhiễu, bỏ, chạy lại độc lập); display flat toàn bộ path có memory; +206 `_refused.txt` marker cho fixture vốn không-decompile từ trước (contract "no stale copies" được đóng); +6 golden dir `m1d1_*`; +7 golden dir g3/g6 (fixture đã track từ slice cũ, golden dir lần đầu được commit); 5 golden dir policy-mới refuse ở loader (`MODULE REJECTED` + diagnostic); expect **48/48** sau khi cập nhật 12 pin sang dạng flat (dwarf_ext/p02/s1/m0a×2/fixed_memory×6 — pin ngữ nghĩa giữ nguyên) |
| Check đổi theo D1 | `memory_space` (MEMORY@0 + .text≥4GiB: layout assert trên p02_bss_access có data active + memory64 phần riêng); `p1_fast` P2-3 pin `void*`==4 → **ptr==8 + int==4** (DB rộng buộc CM_N64 — MERR_SIZEOF); `differential`+`p1_fast` struct-member display (`a1->field_N`) chỉ còn offset-0 — DB rộng làm param 4-byte không mang được con trỏ struct 8-byte (ghi follow-up todo 8-K7) |
| Quan sát môi trường (ngoài phạm vi D1, cần điều tra riêng) | 6 fixture fail load/F5 NHƯNG tái lập y hệt trên **binary rev23** (staged riêng, md5 khớp freeze): `a6_local_assigned_ok`/`wasmhex_call_ref`/`wasmhex_call_ref_same_join`/`wasmhex_reference_ops`/`wasmhex_typed_ref_compatible` (load fail rc=4, "M3B2: instruction walker failed at 1001") + `scalar_select/funcref_local` (INTERR 50526). Bytes fixture == HEAD, bundle IDA không đổi từ 10/2026, deterministic ×5 lần — machine drift giữa 08/11 và 13/11, KHÔNG do D1 (loader/entry.cpp của rev23 không có gate mới). Golden của 6 dir này GIỮ HEAD (không đưa drift cục bộ vào corpus); 19-fixture ORACLE_ISSUES list = tập fixtures INTERR vốn có + 6 drift này |
| Bar khác | build clean-first `build/d1` 0 warning · core_tests **94/94** (92.964 asserts; +6 probe tsv, pin checked 406→412) · fuzz standalone **45.800.458 exec no-crash** (100k iter × 458 corpus) · main100 **ok=100 fail=0** (log sha `4c197e41…`, pseudocode sha `df00537a…`) · balls 5-func smoke **ok=5 fail=0** (log sha `4c375372…`, pseudocode sha `15a8ab28…`) · expect **48/48** · matrix **24/24** · check all **45/45** (+1 `m1d1_layout`) · golden-merge strict regen baseline mới `lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` (rev23: 265/…/68; verify ×2 deterministic `WASMHEX_GOLDEN_OK`) · asan-smoke pass (93s) · format-check sạch · tidy-check sạch · dep_check OK |

### T0 rev25 — D2 PHASE 1: Go stack frame là stack frame của Hex-Rays (RE-BASELINE CÓ CHỦ ĐÍCH sau D2)

**Tính chất rev: RE-BASELINE CÓ CHỦ ĐÍCH sau D2** — hàm Go có frame evidence giờ render qua
stack frame của decompiler (vars/by-value/&var), các legs prologue/epilogue được tiêu thụ;
runner tách `refused` (MERR_BADARCH fail-closed có diagnostic) khỏi `failures` (INTERR/EXC).
Từ rev25 quay lại quy tắc bất biến cho đến thay đổi lowering chủ đích kế tiếp (D2 phase 2).

| Mục | Giá trị |
|---|---|
| HEAD | merge fast-forward branch `m1-d2` (`f846a49` prototype + `c110f58` format + `def820d` marker-arithmetic 0-INTERR; merge-base `de007af` trực tiếp) + commit merge này (rewriter_go.cpp wb-collapse go-frame + golden regen + rev25) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `8724eb73c6d3f89a86f08eb6ef751d10` |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `d6edac3195c4c2cbad46af9d69a5ea6e` |
| Cơ chế D2-P1 | Go frame (blob 'x' GOFR, fs ≤ 0xFFF0, tổng frame ≤ 256KB) map [0,fs) lên frame decompiler qua máy C shadow-stack; incoming window [fs+8,+extent) đi args area (frregs=8, inargoff+off); caller-side ArgInfo-shape arguments BY VALUE (argloc `0:^off.size`); `&stack_cell` escape → `&var`; marker lan truyền qua extend/add/wrap. Marker-arithmetic 0 INTERR bằng MỘT quy tắc: địa chỉ materialize 4B + mọi consumer 8B xdu đúng một lần (`widen_materialized_address`) tại 5 sink còn thiếu (local.set i64, i64.add non-const, m_stx value ×2, helper arg, by-value replay) + 2 lỗ holes vòng đời (call arg tái dùng op, replay marker rỗng) — 66 INTERR → 0 |
| Cơ chế wb-collapse go-frame (commit merge) | `rewriter_go.cpp`: recognizer G6 viết quanh SP-global model — với go-frame, spill cells là stack vars và SP refresh bị protocol tiêu thụ. Nới 4 điểm có điều kiện `spill_free_body` (không spill machinery): (1) `valid_sp_refresh` không đòi refresh khi không có spill; (2) reload loop chấp nhận alias var-var (`LODWORD(v)=v_tmp`) — lhs qua `strip_casts_and_lodword`; (3) post-store match khi không còn slot để đối chiếu; (4) nil-guard lặp sau if render qua alias (`v2=v8` body/hoisted, `if(v2==0)` sau if) — resolve bắc cầu qua alias map seed từ cả statements trước if. Kết quả: 3 fixture g6_wb_* collapse trở lại (pins 48/48 nguyên dạng); balls wb 49→82/224 — **142 hàm còn skip** (shard 1/3: 125 `extra_side_effect_in_body` + 43 `escaping_temp` + 7 khác) — **nợ D2-P2**: recognizer stack-var cho các hình spill/guard phức tạp hơn |
| **F5-subset (DECISIONS #23)** | duckdb hidden+explicit **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` BẤT BIẾN rev1→rev25** (634/634 ×2 — C-path vô hại qua mọi sink chung) · balls **`9453addc7dc2212a12e35032ec1b204e4f0e57ee579f805d2d6c1fe9fcf8bddc`** (rev24 `36d4127a…` ; nguồn đổi: 10 refused tách khỏi failures trong PROGRESS lines + display frame-model) · g126 **`4dd5ee7210330bfea8bea3833ca8b5d063cc9e7221bbeea36efc70dbd69f8396`** (rev24 `604af852…`) · main full sweep **`07953169f7608c5424d3a3b2e29d392f721877bc814d0e330e8cfcab3251d117`** (rev24 `4d333ba8…`) — 3 corpus sau: 0 INTERR, refused fail-closed 10/12/15 (SP re-anchor quanh call không ghi nhận — đích Step C phase 2) |
| Machine-categorized balls (dump 2 chiều D1 `after` vs D2) | 1.759 = identical **243** · đổi **1.506** = `go-frame-model` (SP-window→stack vars/by-value/&var; dẫn chứng `main.__Ball_.draw` 47→0 store SP thô, 13 aggregate by-value, 12 `&var`; `m0a_go_result_slot/wasm_func_1` co còn 2 dòng đọc arg-cell) + `wb-collapse` khôi phục 82/mất 142 (trên 220 hàm có wb ở D1; 80 giữ chú thích trong tập đổi + 2 identical) + `refused-fail-closed` **10** (`internal_abi.NewName`, `internal_strconv.fmtX`, `runtime.{__pageAlloc_.find,exitsyscall,itabAdd,pkgPath,randinit,saveAncestors,traceback1,tracebackPCs}`) · **0 INTERR** |
| `tests/golden/` regen | đúng 4 fixture đã định danh + tác động wb-fix: **13 file** (8 .c + 4 _metrics.json + 1 _refused.txt timestamp) trong `g6_wb_1slot` (2 .c: collapsed khôi phục chú thích + near-miss body frame), `g6_wb_2slot` (2 .c), `g6_wb_structural_guard` (3 .c + refused ts), `m0a_go_result_slot` (1 .c: 2 dòng arg-cell). Dump chéo toàn 470 fixture ×2: **0 file .c nào khác đổi** ngoài tập này |
| Bar khác | core_tests **94/94** (92.964 asserts) · expect **48/48** (pins g6 nguyên dạng sau wb-fix) · matrix **24/24** (`WASMHEX_BRTABLE_OK`) · check all **45/45** · golden-merge `WASMHEX_GOLDEN_OK lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` (bất biến rev24) · fuzz standalone **45.800.458 exec no-crash** · asan-smoke pass (57s) · format-check sạch · tidy-check sạch · dep_check OK · balls evidence table so nguyên mẫu: draw/loop/Sprintf giữ nguyên mọi chỉ số (0/6/2 store SP thô, 0 SP-global ref, 13/4/0 by-value, 12/4/0 &var) — draw normalized-diff **0 dòng** so nguyên mẫu, flat-render của D1 cộng thêm (`byte_X[v]` → `(unsigned int)v`) |

### T0 rev26 — D2 PHASE 2 (milestone 1): marker-op family đóng hết — F5-subset PHỤC HỒI rev24

**Tính chất rev: F5-subset 4 corpus quay về ĐÚNG hash rev24** — "deliberate degrade"
(refused 10/12/15) của rev25 hóa ra là họ marker-arithmetic cuối cùng chưa nhìn thấy,
không phải SP re-anchor như phán đoán ban đầu. Không re-baseline display (display giữ
khuôn rev25: go-frame-model); status-lines trùng rev24 vì 0 fail + 0 refused.

| Mục | Giá trị |
|---|---|
| HEAD | `D2 phase 2 milestone 1: incoming…` ( xem git log ) ← `2e9cd31` (rev25) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `f7fccfba8c0a15ca3f41b1f956764fdc` |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `f3adfbcf65d6b694a50ee944901c0c28` |
| Cơ chế | 4 sink marker cuối cùng trong `lower_numeric.cpp`, cùng quy tắc widen 4B→8B một lần của P1: (1) `emit_compare` i64 (`i64.ne/eq/ge…` — sentinel end/capacity; 6 hàm balls: NewName, saveAncestors, randinit, tracebackPCs, pkgPath, exitsyscall); (2) `emit_integer_binary` i64 (`i64.sub` — fmtX end-pointer math); (3) `emit_eqz` i64 (wasm_func_3262 main.wasm); (4) `emit_global` set — CTXT store (`global.set 1`) của chuỗi PC_F với giá trị marker (itabAdd, `__pageAlloc_.find`, traceback1) — materialize + widen theo width global |
| **F5-subset (DECISIONS #23)** | duckdb `5f728cb0…` · balls **`36d4127a…`** · g126 **`604af852…`** · main **`4d333ba8…`** — cả 4 **TRÙNG rev24**, refused 0/0/0/0, INTERR 0 |
| Machine-categorized balls (dump 2 chiều D1-rebuild × D2-rev26) | 1.753 .c = identical **244** · `go-frame-model` đổi **1.513** · wb-collapse giữ **80** / mất **142** (nợ tiếp: `extra_side_effect_in_body` + `escaping_temp` hình go-frame phức tạp) · **0 refused, 0 chưa giải thích** |
| `tests/golden/` | cross-check toàn 470 fixture ×2 với binary rev26: **0 file .c đổi** so rev25 |
| Bar khác | core_tests **94/94** · expect **48/48** · matrix 24/24 · check all **45/45** · golden-merge `WASMHEX_GOLDEN_OK lines=273 … lvars=71` (bất biến) · fuzz standalone **45.800.458 exec no-crash** · asan-smoke pass · format/tidy/dep pass · (môi trường: sau restart phải build bằng CLT clang21 — `/usr/bin/c++` là Apple clang 15 không hiểu CLT 27.2 SDK headers; asan tree rebuild sạch) |
| Còn của D2-P2 (chưa làm) | Step C — incoming window thành stack params + tên receiver `go_named_sig` (draw `main_Ball *b`); `x=f(...)` cho stack result đơn (Sprintf phantom-sret → gán, chỉ kNeutral của M0-A); 142 wb-collapse go-frame còn skip |

### T0 rev27 — D2-P2 Step C: incoming window thành stack params (evidence-gated, 616 hàm balls)

| Mục | Giá trị |
|---|---|
| HEAD | `D2-P2 Step C: incoming stack params (evidence-gated)` ← `7df3c32` (rev26) |
| `~/.idapro/procs/wasmhex.dylib` md5 | `f7fccfba8c0a15ca3f41b1f956764fdc` (không đổi rev26) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `d7cc384cc3693d4523c171317931dad6` |
| Cơ chế | `loader/entry.cpp`: hàm go-frame có ArgInfo shape proven (blob 't') khai báo stack params cho window [SP+fs+8+off] — decl `__usercall dummy(ret loc)(int a1@<r0>, <type> aN…)` với PC_B giữ r0; params KHÔNG argloc (Hex-Rays tự xếp vào args area frame đã reserve — frregs=8/argsize của M1-D2, đúng vị trí cells). Types: named-sig complete → c_name struct (ensure_go_struct_type, size cross-check shape bytes); scalars theo bytes (4→int, 8→__int64); group không named type → fail-closed cả chữ ký. Counters `go frame step-C params` (upgraded/shape-skip/parse-skip). Bài học cú pháp: parse_decl cần dummy-name (tên Go có dots) + `__usercall` + ret-loc chỉ khi ≠ void |
| Kết quả balls | **616/1759 upgraded** (lớp `go-frame-params`); 585 shape-skip (không ArgInfo proven) + 61 parse-skip (float/xmm + edge). `internal_abi.__Type_.DescriptorSize(int a1@<r0>, __int64 a2)` — thân DÙNG `a2` thật (nil-check `if (a2==0) sigpanic` + `byte_14[(unsigned int)a2+3]`) — bớt load ô window đúng nghĩa Step C. draw: `(int a1@<r0>, __int64 a2)` — receiver scalar 8B (named sig incomplete → giữ aN, chưa typed `main_Ball *` — nợ tiếp theo đích typed-receiver) |
| **F5-subset** | balls **`36d4127a…` trùng rev24/rev26** (1.759/1.759, 0 INTERR, 0 refused) · g126 `604af852…` trùng · duckdb ×2 **`5f728cb0…` bất biến** · core_tests 94/94 · expect 48/48 · golden-merge OK bất biến |
| Bar khác (đóng nợ rev27, chạy trên deploy rev27 `e1c1401d…`/`0855f6d3…` build lại từ HEAD `6cffaca`) | main sweep **3.255/3.255** failures=0 F5_SUBSET **`4d333ba8ae9284b9d3c0367aef0db3ab5fd07f788a04016105da286bd52e3901` trùng rev24** · fuzz standalone **45.800.458 exec no-crash** (100k iter × 458 corpus) · asan-smoke pass (34,8 s) · tidy-check sạch · dep_check OK (build/core_tests) · balls f5 tái lập `36d4127a…` |
| Còn của D2-P2 | `x=f(...)` result đơn (chưa mở — token session cạn, mọi cơ chế verdict M0-A đã sẵn); typed receiver struct cho sig-incomplete (draw `main_Ball *b`); Sprintf chưa nâng (named+variadic — thuộc 61 parse-skip/585 shape-skip, phân tích sau) |

### T0 rev28 — D2-P2: x=f single results (eligibility + phân lớp; apply pending engine) + sret-keep classification

| Mục | Giá trị |
|---|---|
| HEAD | `8b98dee` ← `6cffaca` (rev27) |
| `~/.idapro` md5 (đợt build final rev29) | procs `8b762b1615a80f77ef4448fe11d4b8a9` · loaders `8edd6abf395c4269ba57afc8f40bd0db` (loaders 14 marker strings; md5 đổi mỗi lần build — linkage non-deterministic, source rev26/27 procs không đổi)
| Cơ chế | (1) **Phân tích gốc parse-skip 61**: debug từng decl (WASMHEX_DEBUG=gocall + runner echo goloader) cho thấy 59/61 là hàm multi-result có shape — decl `<first-type> __usercall dummy(...)` không retloc (code cũ chỉ gắn retloc khi results.size()==1) → parse_decl từ chối. **sret-keep** bucket mới: multi-result không x=f giữ nguyên prototype sret `void f(int a1@<r0>, void *aN@<r1>)` (decl stack-param đòi non-void return không retloc — parse từ chối; tuple return phá call-site i32 — INTERR 52849). Sprintf rơi đúng bucket này (kBoundArgument của M0-A — không x=f-eligible). (2) **go_xf_single_result** (core types.cpp): gate đủ — read-coverage G5-S2b (results_caller_proven) + single named composite result (sig.results.size()==1, size == slot extent tile, c_name buildable) + shape proven (ArgInfo) + slot layout returnable (≤6 words, mirror multi_result_word_count không Hex-Rays dep) + **không caller non-frame** (mọi caller frame_valid + có br_if unwind — bằng chứng structural kNeutral: frame caller truyền by-value window cells, không bao giờ chiếm register slot phantom). (3) **'U' wire record** (metadata.hpp go_xf_wire_t v1: min_offset/extent/slot_count/c_name[48]) tại entry EA — nguồn duy nhất cho call-site + F5-side (đọc version-gated, fail-closed). (4) Loader prototype surgery: parse void-form + graft rettype (named struct, get_named_type BTF_STRUCT + size cross-check) + retloc make_multi_return_location + `FTI_ARGLOCS\|FTI_EXPLOCS` (không EXPLOCS engine bỏ qua scattered retloc). (5) filter.cpp F5-side mirror: 'U' → derive make_wasm_function_type (tuple) + swap rettype named (cùng size) — applied==derived. (6) **ÁP DỤNG ĐANG TẮT** (`kGoXfApplyEnabled=false`): đo trên balls bật — 2 hàm (fmt.Sprintf, syscall_js.Value.String) parse+surgery OK nhưng F5 callee chết **INTERR 52849**: mba go-frame với scattered-return build setup return-regs đến ev=4 rồi **m_ret biến mất** → reconcile chữ ký cuối fail (cùng engine check G5-S2b từng gặp — giờ bắn trong chính callee). Bước kế tiếp đã ghi trong comment: retvar route (pair_target nhận `result; // 0:rax.12` từ cùng retloc scattered) hoặc call-site-first. Đếm `x=f single results: enabled=0 eligible-pending=5` (balls: Get/Call/New/String/Sprintf) |
| Kết quả balls | step-C counters đổi phân lớp đúng nguyên nhân: upgraded=616→**624** (sret-keep giúp 8 hàm multi trước đây rớt), parse-skip 61→**2** (2 float edge còn lại), **sret-keep=133** (multi-result giữ sret — trong đó Sprintf), shape-skip 585→503 (phân lớp lại); eligible-pending=5. Dump 2 chiều vs rev27-base: **1757 .c = 1756 identical + 1 đổi** (`syscall_js.__Error_.Error`: `go_agg16`→`go__js_Error` + `v4.Value.ref/.gcPtr` — hiệu ứng phụ của metadata.cpp parse-fix ensure_go_struct_type field: parse_decl cần declarator+semicolon (`"<bare name> x;"`) và từ chối `struct NAME` trần — struct lồng giờ đăng ký được qua đường named-param F5) |
| **F5-subset** | balls **`36d4127a…` trùng rev24→rev28** (1.759/1.759) · g126 **`604af852…` trùng** (1.794/1.794) · core_tests **94/94** · expect **48/48** · matrix 24/24 · check 45/45 · golden-merge `lines=273 … lvars=71` bất biến |

### T0 rev29 — D2-P2: typed receivers cho method sig-incomplete (draw `go__main_Ball *a2`)

| Mục | Giá trị |
|---|---|
| HEAD | `4366598` ← `8b98dee` (rev28) |
| Cơ chế | (1) **go_receiver_c_param** (core types.cpp): method sig-incomplete (anchor-4 gap-cut) vẫn CHỨNG MINH receiver — các slot đầu window tile receiver words; resolve display → descriptor (root struct trực tiếp hoặc `*T` elem cho struct ngoài walk region — mirror go_c_struct_for_param) → c_name; **pointer receiver = 1 word 8B** (con trỏ Go trên wasm 8 byte — xác nhận bằng bytes ArgInfo draw `{0:8}` + reads `a2+32/40` = color@32), value receiver = struct by value đúng descriptor size. (2) Step C branch: param đầu window nhận `go__X *aN` (pointer) / `go__X aN` (value) khi lead cell khớp (scalar 8B / group đúng bytes); mọi fail → giữ scalar proven (KHÔNG sập signature — bài học phiên đầu: ensure-fail từng làm mất upgrade của 66 hàm). (3) **ensure_step_c_receiver_struct**: pre-ensure các canonical field carriers (go__string/slice/iface/js__Value — strip prefix `struct ` trước so khớp) trước khi ensure struct; **split pad >8B thành các field 8B** (`__int64` member không mang size 16 qua create_udt — main.Ball hố 16B sau y fail đúng đây). (4) Counters `go frame step-C receivers: typed/skip`. |
| Kết quả balls | **receivers typed=22 skip=102** (skip = descriptor không build được / lead-shape không khớp — đếm theo lý do ở debug). **draw: `void __usercall main.__Ball_.draw(int a1@<r0>, go__main_Ball *a2)`** — nil-check tự nhiên `if (a2 == nullptr) sigpanic` (đúng ngữ nghĩa *main.Ball); reads color vẫn flat `byte_14[(unsigned int)a2+12/20]` (member display là K7 — DB rộng, việc của G8/ctree). Machine-categorized balls (dump 2 chiều rev27-base × rev29): 1.757 .c = identical **1.738** · **`go-receiver-typed` (pointer) 11** (draw, reflect.__Value_.String, runtime.__waitq_.dequeue, sync.__Pool_.pinSlow v.v.) · **`go-receiver-value-typed` 8** (reflect.Value.{String,Pointer,NumField…} — struct by value `go__reflect_Value a2`) · **0 dòng ngoài 2 lớp** |
| **F5-subset** | balls **`36d4127a…` trùng rev24→rev29** (1.759/1.759, 0 INTERR, 0 refused) · g126 `604af852…` · duckdb ×2 `5f728cb0…` (bar đầy của rev28+29 chạy chung, xem rev28/29 bar khác) |
| Bar khác (rev28+29 chung, final HEAD `4366598` + unit-pin commit) | core_tests **95/95** (92.971 asserts; +1 TEST_CASE `D2-P2: x=f single-result eligibility and typed receivers` pin eligible=5 balls + Sprintf [40,56) go__string + draw receiver *main.Ball 8B go__main_Ball) · expect **48/48** · matrix **24/24** · check all **45/45** · golden-merge `lines=273 … lvars=71` bất biến · main sweep **3.255/3.255** F5_SUBSET **`4d333ba8…` trùng rev24** · fuzz standalone **45.800.458 exec no-crash** · asan-smoke pass · tidy-check sạch · dep_check OK |
| Còn của D2-P2 | `x=f(...)` APPLY: engine scattered-return trên go-frame callee (INTERR 52849, m_ret biến mất sau ev=4) — mọi gate + wire + prototype machinery sẵn trong rev28, next: retvar route / call-site-first; 142 wb-collapse go-frame; 2 parse-skip float edge; member display cho typed receiver (K7/G8) |

### T0 rev30 — M3 display trio: G6 string literal + G7 boxing/variadic + G8 member annotation (todo 12/11 §5 — hoàn thiện pseudocode đích `draw`)

| Mục | Giá trị |
|---|---|
| HEAD | M3 G6/G7/G8 (commit riêng theo mục: core helpers + filter display pass) ← `3fd4866` (rev29) |
| `~/.idapro` md5 (deploy bar cuối) | procs `6883e35b156b9b753d83b3ab4f3bb307` · loaders `374687895984773e03dc906b12e49c5a` (procs 15 marker strings incl. `WASMHEX_BUILD_MARKER_M3_GO_VALUE_DISPLAY`). Lưu ý môi trường: build `asan-smoke` (tree build/asan) GHI ĐÈ loaders vào SDK bin chung — `cmake --install` sau đó đem bản ASan vào `~/.idapro` và mọi runner chết `Interceptors are not working` (asin load qua LC_LOAD_DYLIB của wasmhexldr); fix = xóa SDK bin loaders + relink build/d1 + install lại; bar cuối chạy ASan-smoke SAU cùng mọi install |
| Cơ chế | (1) **G6** `collect_go_value_evidence` (filter.cpp, chạy trong reset_state sau quyết định go-frame): với mỗi shape param 16B `go_string`/`go_agg16` có 2 window store đều push `i64.const` → (ptr, len); ptr là EA data thật (D1), đọc đúng `len` byte, validate UTF-8 + không NUL + len ≤ 1MiB + ptr KHÔNG resolve descriptor (iface {type,data}) → stash literal; callback CMAT_FINAL (`go_arg_literal_swap_t`) swap arg của cot_call tại (call EA, argidx) bằng `make_go_string_literal` (ctree_types.cpp — cùng shape cot_str/EXFL_CSTR của rewriter_idiom); text escape C (`\n \t \r " \\ \xNN`) trước khi stash. Fail-closed từng bước → giữ numeric. (2) **G7** boxing pair: index MỘT lần mọi `i64.store` address-const (3 form codegen: quintet+const-value, quintet cuối, bare-base/SP+k-value) → pair = type store const resolve descriptor (loại Map/Func/Iface) + store tại +8; static data đọc theo kind (`go_boxed_scalar_text`: Bool/Int/Uint/Float32/Float64, double shortest-round-trip %.{15..17}g) → note `// go: box(kind) = value`; **BSS zero-read** `go_read_linear_memory` (byte chưa load trong segment MEMORY = zero-init theo wasm spec — 66176 `any(0)` đọc được 0); động → `// go: box(kind)`; exclusion list leading-arg của runtime metadata family (makemap*/convT*/typedmemmove…) — là type metadata, không phải box. Variadic: shape cuối `go_slice` {ptr=SP+K0, len=N const} + backing [K0, K0+16N) tile đủ N pair → `// go: args = {…}` (all-or-nothing). (3) **G8**: pointer-receiver từ tên hàm `pkg.__T_.m` → descriptor kind-25 duy nhất → mọi access shape `[local.get SP; i64.load frame+8] wrap [+K add wrap] access memarg M` → field covering (`go_member_covering` core, size qua layer) → note `// go: a2->field` đặt tại STATEMENT chứa access (`go_member_note_placer_t` — leo `find_parent_of`, merge nhiều access/statement; không còn orphan). Honest-decline member display: receiver `go__main_Ball *a2` ĐÃ 8B (D2-P2) nhưng địa chỉ wasm i32-wrap giữ domain 4 byte — engine chỉ render offset-0 (`a2->x`), mọi offset khác flat `(unsigned int)a2 + k` — bằng chứng dump rev29; annotation là đích trung thực (K7 còn mở). |
| `draw` SAU (đối chiếu todo §2) | `syscall_js.Value.Call(g, "beginPath", …)` · `Value.Set(g, "fillStyle", …)` + `// go: box(string)` · `Call(g, "arc", …)` + `// go: args = {float64, float64, float64, int = 0, float64 = 6.283185307179586}` + `box(float64)`×3 + `box(int) = 0` + `box(float64) = 6.283185307179586` · `Call(g, "fill", …)` · `x = a2->x` (native) + `// go: a2->color` ×2 + `a2->y` + `a2->size` tại mọi field access · nil-check `if (a2 == nullptr) sigpanic` giữ · Sprintf `b->color` qua 2 statement + window stores thật |
| Kết quả balls | Machine-categorized (dump 2 chiều 3fd4866-base × rev30): 1.759 .c = identical **1.158** · changed **599** = `go-string-literal` **405** · `go-boxing` **186** · `go-member` **130** · `go-variadic-args` **10** · **0 uncategorized** (4 file literal-chứa-newline ban đầu cũng về lớp literal sau khi thêm escape C). Ví dụ: `runtime.throw("missing deferreturn")`, `runtime.printstring("\n")`, `main.loop` `"fillRect"` + `args = {int = 0, int = 0, float64, float64}` |
| **F5-subset** | balls **`36d4127a…` trùng rev24→rev30** (1.759/1.759, 0 INTERR, 0 refused — status-line không chứa display) · main sweep **3.255/3.255** F5_SUBSET **`4d333ba8…` trùng** · duckdb sample ×2 **634/634 `5f728cb0…` BẤT BIẾN** |
| Bar khác | core_tests **98/98** (93.044 asserts; +3 TEST_CASE categorized: `M3 G6 go-string-literal` — validate UTF-8/NUL/len + 4 cặp (ptr,len) của draw + 3 descriptor addr không-phải-string; `M3 G7 go-boxing` — kind Bool/Int/Uint/Float32/Float64 + shortest 6.283185307179586 + BSS fact 66176 ngoài mọi segment + kind 24/1234288, 14/1235248, 2/1234864; `M3 G8 go-member` — covering x/y/velX/velY/color(ptr+len)/size + straddle/past-end false) · ctest build/d1 **4/4** (format/tidy/core_tests/runtime_oracle) · expect **48/48** · matrix **24/24** · check all **45/45** · golden-merge `lines=273 … lvars=71` bất biến · fuzz standalone **45.800.458 exec no-crash** (100k iter × 458 corpus) · asan-smoke pass · dep_check OK. Form B/C address của pair-scan siết thêm sau commit đầu: base bắt buộc là SP-cache local (frame anchor) — recount balls GIỮ NGUYÊN 405/186/130/10 (không case nào mất) |
| Ghi chú | `test_spec_oracle BackendSupport checked==412` fail **pre-existing** — tái lập y hệt trên HEAD sạch `3fd4866` (đã đo git stash; nợ môi trường/corpus oracle-invalid, không liên quan M3); binary `build/core_tests/core_tests` cũ không relink — chạy đúng `build/core_tests/native/wasmhex/tests/core_tests` |
| Còn của M3 | G3 (FuncType descriptor ưu tiên trước bằng chứng — Sprintf param0 vẫn named `go__js__Value` qua agreement sai từ `Value.Invoke`); G9 giữ tên `main.(*Ball).draw`; x=f APPLY (INTERR 52849); wb-collapse go-frame 142 |

### T0 rev31 — G10 morestack prologue ở IR + G9 tên `main.(*Ball).draw` (todo 3.5/3.6 — slice `draw` của Root)

| Mục | Giá trị |
|---|---|
| HEAD | `06ac446` (G10) ← `0c86065` (rev30); `6e5bb63` (G9) ← `06ac446`. Deploy bar: `build/g10/idausr` procs md5 cuối (build CLT clang 21) |
| Cơ chế | (1) **G10**: matcher ctree P0-6 lỗi thời (lowering G3 tiêu thụ store trong then-block — todo 3.6). Chứng minh chuyển về IR: `collect_go_prologue_evidence` (core/toolchains/go/ir.cpp) verify chuỗi lệnh compiler thật quanh call morestack[_noctxt] đã chứng minh (pclntab hoặc wrapper-body 20-byte P0-6 fallback) — guard compare `local.get L; global.get 2; i32.wrap_i64; i32.load a=2 off=16; [i32.const K; i32.add]; i32.le_u|lt_u` + `if` + SP−=8 leg + `i64.store a=3 off=0` resume-PC + `[i32.const 0]` noctxt + call + refresh pair + `br_if` + `end`; record `go_prologue_ir_t` ride blob 'x' tail **GOMS v1** (append sau GOFR, tương thích ngược: reader cũ dừng trước tail, blob thiếu tail = không mark); round-trip + fail-closed từng opcode (verify ngược từng lệnh với bounds-check). Ctree layer (rewriter_go) chỉ consume mark: xóa `if` tại `entry+if_offset` sau verify decode là `wasmhex_if`; **xóa matcher cũ** guard_deref/block_shape_matches/is_morestack_callee/is_le_lt (mục 6 bảng "code kiểu vá"). (2) **G9**: đo idat 9.5 probe — `set_name(SN_FORCE)` nhận `main.(*Ball).draw` chỉ sau `set_cp_validity` mở `(` `)` `*` ở CẢ UCDR_NAME + UCDR_MANGLED → processor `ev_init` mở; `go_method_display_name` (core/module.cpp) `pkg.__T_.m`→`pkg.(*T).m` (mirror anchor-4; value `pkg.T.m` display chuẩn giữ nguyên; package-less không đổi); entry.cpp đặt tên IDA dạng display, pclntab/export giữ mangled làm ground truth; filter.cpp G8 receiver parse nhận cả `__T_` (IDB cũ) lẫn `(*T)` (mới — cùng substr(2,size-3)); `safe_func_name` của runner map `(*Ball)`→`__Ball_` nên tên file golden KHÔNG đổi |
| `draw` SAU (đối chiếu todo §2) | Guard `if ((unsigned int)v3 <= *(_DWORD *)&byte_10[(unsigned int)wasmhex_global_2] + 64) runtime.morestack_noctxt()` **BIẾN MẤT** (dòng đầu tiên giờ là copy `ctx` js.Value); frame nhỏ `main.main.func1` cũng hết `if (… <= *(_DWORD *)&byte_10[…]) runtime.morestack()`. Tên hàm: `void __usercall main.(*Ball).draw(int a1@<r0>, go__main_Ball *a2)` + comment `go: (*main.Ball) draw(…)` — đích todo §2 đạt tên chuẩn; nil-check + mọi annotation G6/G7/G8 giữ nguyên |
| Kết quả balls (machine-categorized, dump 2 chiều vs rev30-base) | **G10**: 1.385 .c đổi = đúng 1.385 guard-if + 1.385 call morestack bị xóa, **0 dòng thêm ngoài metrics** (4.908 dòng xóa tính cả count-lines). **G9** (bật sau G10): 692 .c đổi = 1.919 dòng thêm **100% chứa `(*`** / 1.919 dòng xóa **100% chứa `__`** — **0 dòng ngoài lớp `go-prologue-hidden` + `go-method-name`**. Annotation G8 612→612 (kể cả `runtime.(*p)` type 1 ký tự — edge size≥4) |
| IR coverage (test G10 survey) | balls: ir=1.741, morestack=1.388, **proven=1.387, skipped=1** (skip = body 1365 `runtime.morestack_noctxt` — wrapper 20-byte không có guard riêng, fail-closed đúng) · main126 stripped: **proven=2.841, skipped=1** (P0-6 structural fallback nuôi matcher — không phụ thuộc symbol) |
| **F5-subset** | balls **1.759/1.759 0-interr `36d4127a…` trùng rev24→rev31** · main sweep **3.255/3.255 `4d333ba8…` trùng** · duckdb sample **634/634 `5f728cb0…` BẤT BIẾN** (bar chính DECISIONS #23) |
| Bar khác | core_tests **100/100** (97.325 asserts; +2 TEST_CASE: `G10 morestack prologue` — draw framesize_extra=64/ordered offsets/round-trip/blob cắt giữa GOMS fail-closed/blob pre-GOMS decode không mark; survey balls 1387/1 + main126 2841/1; `G9 method display names` — 5 cặp transform + 8 near-miss giữ + ground truth mangled còn trong module) · expect **48/48** (p06 golden regenerate theo fixture đúng compiler-shape) · check **45/45** (p06 named/unnamed giờ qua IR proof) · matrix **24/24** · golden-merge `lines=273 … lvars=71` bất biến · format/tidy pass · dep_check OK (IDASDK env cần cho graphviz regen) · asan-smoke pass · fuzz standalone **45.800.458 exec no-crash** (100k iter × 458 corpus) · ballclone f5 1.618/1.618 hash `b3741370…` trùng base |
| Ghi chú Việc 1 (counter pre-existing) | **Đối chiếu xong — counter ĐÚNG, không sửa**: tsv có đúng 412 valid/14 invalid khớp cả hai counter `checked==412`/`==14`. Fail M3 thấy là do chạy nhầm **bản sao cũ** `build/core_tests/core_tests` (11 Nov, counter 404 thời đó) nằm ở root build dir — không phải target output (target thật `build/core_tests/native/wasmhex/tests/core_tests`, và cũng chính là binary ctest chạy — 98/98 rồi 100/100). Binary cũ này đọc tsv MỚI lúc runtime → `CHECK(412 == 404)`. Hai "config build khác nhau" của M3 = 2 binary khác nhau, không phải 2 config |
| Ghi chú fixture p06 | `p06_morestack_names.wat` nâng lên đúng byte wasmobj.go: callee `(i32)->i32` (type[0] mọi corpus — result là điều kiện br_if), resume-PC là `i64.store a=3 off=0`, thêm `br_if` đóng then-block. Matcher cũ chấp nhận store i32 hằng bất kỳ — IR proof muốn đúng compiler; named ẩn hoàn toàn (`v1 = wasmhex_global_0; return 42;`), unnamed giữ nguyên guard + `runtime.mini(0)` + 4660 |
| Còn mở | duckdb FULL sweep (63.358 hàm, ngoài bar): **2 INTERR 51943 pre-existing** — tái lập y hệt trên deploy baseline `6883e35b…` (0x1005de4d1 `DuckDBConstraintsFunction` + 0x10102dfdf `wasm_func_32497`, shard hẹp verify) — không phải do G10/G9; bar sample 634 vẫn `5f728cb0…`. wb-collapse go-frame 142 · x=f APPLY (INTERR 52849) · G3 FuncType-first · G9 phần global `g_<addr>` |

### T0 rev32 — G3 layout tham số/kết quả MỘT NGUỒN (todo §5 G3 + §6 bảng vá — Sprintf param0 `go__string` trên `draw`)

| Mục | Giá trị |
|---|---|
| HEAD | G3 (commit `G3: single-source Go param/result layout (descriptor > ArgInfo > evidence, confidence-tagged)`) ← `563730e` (rev31). Deploy bar cuối: `~/.idapro` procs md5 `01cf84b677b5b2719dd6a24bdbccb8af` · loaders `fe9f073757fde66df449f0bfe3b7c2a3` (build CLT clang 21, `build/d1`, staging idausr `g3` same dylibs) |
| Cơ chế | (1) **Bản ghi layout một nguồn** `go_func_layout_t` per defined body — builder `go_build_func_layouts` (types.cpp) chạy cuối `module_t::parse` (name callback mirror `function_name` loop entry.cpp, dedup cùng thứ tự): tier (a) `kDescriptor` = `go_resolve_named_signature` complete ghép 1:1 byte-range với shape ArgInfo (params named + results tại offset `_func.args` gap + fields; one-word aggregate KHÔNG demote khi được typed); (b) `kArgInfo` = shape expansion (demote one-word như display walk cũ); (c) `kEvidence` = (b) + agreement scan size-agreeing descriptors (port verbatim từ filter, chạy trên snapshot descriptor `blob_descriptor_cap` = scan output) + receiver-value naming theo tên mangled (port verbatim; value-receiver canonical nên == display). (2) **Lưu trữ**: blob 't' **v2 tail** (mọi field prefix v1 giữ nguyên; v1 stream decode → layouts rỗng = canonical-only, fail-closed; lý do chọn 't' vs 'x': module-level keyed defined-body như arginfo + collect đã decode 't' — DECISIONS #33). (3) **Hai phía cùng đọc**: Step C (entry.cpp) lấy shape + typed groups từ bản ghi (fallback: arginfo walk khi record vắng; nparam positional path giữ làm fallback arity-mismatch); `collect_go_call_shapes` (filter.cpp) xóa agreement scan + receiver naming inline, thay bằng lookup bản ghi với mapping chính tắc cũ (`go__string`→`go_string`…, go__X giữ fields). (4) **Gate KHÔNG nới**: verdict result-slot M0-A + read-coverage G5-S2b + mọi per-call window proof giữ nguyên. Census line mới `wasmhex: go func layout (descriptor > arginfo > evidence): …` |
| `draw` SAU (đối chứng todo §2 + đích G3) | `((void (__usercall *)(go_string, go_slice))fmt.Sprintf)(v3[0], *(go_slice *)&v3[1].tab);` — **param0 typed `go__string`** (trước: `go__js__Value` từ agreement sai `Value.Invoke` — tier (a) known-API `func(string, ...any) string` thắng); `Value.Call` `(go__js__Value, go_string, go_slice)` (param1 trước `go_agg16`); `Value.Set` `(go__js__Value, go_string, go_iface)`; literal swap G6 `"beginPath"`/`"fillStyle"` + mọi annotation G7/G8/G10 giữ nguyên |
| Census (bar "đếm corpus") | balls: **descriptor=30 · arginfo=1312 · evidence=48 · skip-no-arginfo=369 · skip-shape=0 · total=1759** (loader line + TEST_CASE pin `G3: layout census`; Sprintf/Value.Call tier-(a) pin riêng) |
| Kết quả balls (machine-categorized, dump 2 chiều vs `563730e` base) | **1741 identical · `go-layout-descriptor` 12 file** (draw/loop/main/func1/handleEvent/FuncOf/mapJSError/init×4/hexdumpMarker/printcomplex64 — cast `go_agg16`→`go_string`/`go_iface`/`go__js__Value` đúng anchor) **· `go-layout-arginfo` 4 file** (Step C gain `__int64` params — one-word group demotion thống nhất shape hai phía: `runtime.__pageAlloc_.find.func1`, `runtime.printcomplex64`…) **· 0 uncategorized / 0 file ngoài lớp** |
| **F5-subset** | balls **1.759/1.759 0-interr `36d4127a…` trùng rev24→rev32** · main sweep **3.255/3.255 `4d333ba8…` trùng** · duckdb sample ×2 **634/634 `5f728cb0…` BẤT BIẾN** (bar chính DECISIONS #23) · ballclone **1.618/1.618 `b3741370…` trùng** |
| Bar khác | core_tests **100/100** (97.603 asserts; +2 TEST_CASE G3: Sprintf tier-(a) pin — param0 `go__string` NOT `go__js__Value`, result `go__string`@40, blob round-trip + cap pin; census pin descriptor≥24 + Value.Call `(go__js__Value, go__string, go__slice)` + m0a fixture fail-closed) · blob 't' v2 round-trip + corruption fail-closed + v1-compat decode (empty layouts) cập nhật · ctest build/d1 **4/4** (format/tidy/core_tests/runtime_oracle) · expect **48/48** · check all **45/45** · matrix **24/24** · golden-merge strict `lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` bất biến · fuzz standalone **45.800.458 exec no-crash** (100k iter × 458 corpus) · asan-smoke pass · dep_check OK (IDASDK env) |
| Ghi chú | Danh mục builder chạy TRƯỚC blob write (cuối parse) nhưng `blob_descriptor_cap` đóng băng wire descriptor set == pre-G3 — mọi consumer khác của blob 't' (type notes, member notes, box) thấy đúng tập cũ, đúng cơ sở zero-churn 1741 file. Agreement scan + receiver naming giờ CHỈ tồn tại trong builder (một nơi, đã có tier tag); filter không còn suy luận tên |
| Còn mở | G4 (GoCallIR theo giá trị — bỏ con trỏ cửa sổ stack) · G5 chữ ký thật thay `int a1` (PC_B) · G11 unflatten mọi kích thước · x=f APPLY (INTERR 52849) · wb-collapse go-frame 142 · G9 phần global `g_<addr>` |

### T0 rev33 — G11+G12 đóng M3: unflatten mọi kích thước + 5 fixture Go có nguồn (commit KHÔNG đổi engine)

| Mục | Giá trị |
|---|---|
| HEAD | G11+G12 (commit `G11+G12: unflatten verified all sizes; 5 sourced Go fixtures with expected pseudocode (M3 acceptance)`) ← G3 (rev32). Deploy: **KHÔNG đổi** — `~/.idapro` procs md5 `01cf84b677b5b2719dd6a24bdbccb8af` · loaders `fe9f073757fde66df449f0bfe3b7c2a3` giữ nguyên rev32 (không dòng engine nào bị sửa) |
| Cơ chế | Commit thuần test/fixture/docs: (G11) TEST_CASE census bucket mọi hàm IR-proven của 4 corpus theo ngưỡng gate CŨ 64/512 — tổng 8.107/8.354 unflattenable (small 6.806 / mid 1.229 / large 72 / jmps>64 129; 247 refused toàn `pcb-proof` — validity, không size) + pin monster `wasm_func_1806` arms=15.053 valid; (G12) `tests/go/` 5 nguồn .go go1.27.4 + fixture `tests/fixtures/go_*.wasm` (tsv valid ×5, counter 412→417) + golden curated `tests/golden/go_*/` (expected_notes.md pseudocode kỳ vọng + expect.txt). Bảng checklist `draw` phiên bản cuối: todo §2 |
| Số hàm đổi / total | **0 / mọi corpus — không có delta display** (engine + deploy byte-identical rev32; hash tái tạo dưới đây là chứng minh) |
| **F5-subset** | balls **1.759/1.759 failures=0 `36d4127a…` trùng rev24→rev33** · ballclone **1.618/1.618 `b3741370…` trùng** · main sweep **3.255/3.255 failures=0 `4d333ba8…` trùng** · duckdb sample ×2 **634/634 `5f728cb0…` BẤT BIẾN** (bar chính DECISIONS #23; hidden+explicit cùng sha) |
| Bar khác | core_tests **104/104** (97.740 asserts; +2 TEST_CASE `G11: dispatcher replacement covers every size class` + `G12: five sourced Go fixtures`; tsv counter 412→417, unlisted==30 giữ) · expect **53/53** (48 + 5 golden mới go_*) · check all **45/45** · matrix **24/24** · format/tidy pass (build/rebuild-20261107) · 5 fixture Go mới: dump-golden **6.255/6.255 OK, 0 INTERR, 0 jumpout** (1250+1250+1250+1254+1251; `_metrics.json` mỗi golden dir) · balls dump-golden 1.759/1.759 OK 0 INTERR, `0/1.759` file chứa `switch (a1)` (chứng cứ display G11 mới nhất), chỉ 2 file morestack tự thân (runtime.morestack{,_noctxt}) |
| Ghi chú | (1) `tests/go/build.sh` deterministic trên cùng toolchain (rebuild cùng sha256); go.mod ghim `go 1.27` + `toolchain go1.27.4`; sha256 5 fixture trong `tests/go/README.md`. (2) Golden go_* là CURATED (chỉ file hàm đích + _metrics/_validator đầy đủ) — tái tạo: `wasmhex_runner dump-golden go_<name> --out-root /tmp/x` rồi copy file đích (quy trình trong tests/go/README.md). (3) Dump full 5 fixture không commit (~6.250 file) — cùng lý do balls/g126 chưa từng golden-commit; evidence module-wide nằm ở _metrics.json totals |
| Còn mở | như rev32 (G4 · G5 PC_B · x=f APPLY · wb-collapse go-frame 142 · G9 global) — chuyển M4 |


## 3. gowasm-balls — 5 hàm smoke

```sh
T0_MODE=names T0_NAMES="main.main|main.loop|main.__Ball_.draw|main.main.func1|go_buildid" \
  idat … -S<t0_sweep.py> wasm-test/gowasm-balls/main.wasm
shasum -a 256 gowasm.log
```

Kết quả: `T0_SWEEP total=5 ok=5 fail=0` (mỗi hàm mở đầu
`// wasmhex: bounds=hidden`).

sha256 log đầy đủ: `3969932fc31ca67d0c68a88f54517e06e686be6ef7ad722d009fb0bf02355677`

## Quy tắc T1/T2 và các bản cập nhật T0 rev sau

- Sau MỖI move commit (chỉ dời file, không đổi logic): tái tạo nhóm 1–3
  trên cùng binary — sha phải khớp (bar chính là **F5-subset duckdb hidden
  `5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd`** theo
  DECISIONS #23). Sai khác = regress → dừng, báo Root kèm diff.
- Golden corpus trong git: `git diff` sau move phải rỗng với
  `tests/golden/` (T1 chỉ dời file native).
- **Quy tắc cho mọi bản cập nhật T0 rev tiếp theo (rev2+):** mỗi khi một
  commit có chủ đích làm đổi output T0, mục rev mới phải giữ nguyên lịch
  sử các rev trước và bắt buộc kèm đủ 3 yếu tố:
  1. **(i) Số hàm đổi / total** (ví dụ: `1/100` hàm đổi, `99/100` byte-identical);
  2. **(ii) Dẫn chứng bytes hoặc dump cụ thể cho MỖI lớp thay đổi** (kèm
     commit tham chiếu gây ra thay đổi đó);
  3. **(iii) F5-subset (`5f728cb0…`) BẤT BIẾN** qua các rev — trừ khi thay
     đổi là chủ đích trực tiếp ở tầng lowering (như G3).
- Script sweep gốc: `/private/tmp/wasmhex_tri/t0_sweep.py` (in-IDA worker;
  tái tạo nội tuyến nếu tempdir bị dọn — logic: DECOMP_NO_CACHE +
  tag_remove từng dòng, WASMHEX_VERIFY=1, ARG_HINTS_MODE=0).

### T0 rev34 — M2 D3+H1+H2: ModuleIndex + FunctionIR dựng một lần (per-F5 decode bị loại; refactor thuần kiến trúc)

| Mục | Giá trị |
|---|---|
| HEAD | `M2 D3: ModuleIndex + FunctionIR built once (H1/H2) — per-F5 decode eliminated` ← `e62e239` (rev33 = `b7af346` không đổi engine). Deploy `~/.idapro`: procs md5 `dd572133bc02fe5f3e45ba9c10444c10` · loaders md5 `053036306347fc788d655c21fc0909ed` (CLT clang21 `build/d1`; verify sau deploy: balls-100 `663e2d8d…` tái tạo + 100 dòng IRTIME từ binary deployed) |
| Cơ chế | (1) **ModuleIndex** (`core/toolchains/go/index.{hpp,cpp}` mới, SDK-free — G2 tách file): blob 't' (descriptors/ArgInfo/G3 layouts) + blob 'Z' (slot map table-0, port nguyên văn) + biên import 'F' decode **1 lần per IDB**; cache in-memory khóa identity (hash word-stride toàn bytes nguồn + schema; DECISIONS #34); closebase dọn; **wire 't' v2 giữ nguyên** (không tag mới). Descriptor lookup O(1) thay linear scan 6 chỗ. (2) **FunctionIR** per hàm (LRU 16): insn stream {ea,itype,op1} decode 1 lần (trước đây 4 walk/F5: shapes/notes/value/frame-legs), blob 'x' decode 1 lần, memo probe 'j' per call-site (filter + lower_calls); vô hiệu khi body-bytes/blob-'x'/module-identity đổi — kiểm mỗi fetch. Collectors giữ nguyên mọi gate, chỉ đổi nguồn đọc (fail-closed collapses 1:1 vào `layer_present`/`stream_valid`). IRTIME = hook đo mới (WASMHEX_DEBUG=irtime, fd-2 như 'mc'). |
| **Số đo trước/sau (bar riêng M2, balls 100 hàm đầu, 2 run mỗi bản, idalib runner)** | reset_state **4,44 → 1,52 ms trung bình (-66%)** · blob 0,64 → 0,10 ms · body 3,24 → 1,01 ms (1 build/hàm) · **blob-'t' decode 250 → 1 per process** · duckdb stride-100 (đường C, không 'x') **0,39 ms bất biến** — không regress · RSS balls-100 248 → 226 MB (duckdb 1,32 → 1,36 GB — biến thiên engine, không có cache nào được build trên đường C: blob_builds=0 body_builds=0) |
| Số hàm đổi / total | **0 / mọi corpus — refactor thuần kiến trúc** (mọi khác biệt display = regress; không có khác biệt nào) |
| **F5-subset** | balls **1.759/1.759 failures=0 `36d4127a…` trùng rev24→rev34** · ballclone **1.618/1.618 `b3741370…` trùng** · main sweep **3.255/3.255 `4d333ba8…` trùng** · duckdb sample ×2 **634/634 `5f728cb0…` BẤT BIẾN** (bar chính DECISIONS #23; hidden+explicit cùng sha) |
| Bar khác | core_tests **108/108** (101.115 asserts; +4 TEST_CASE M2: identity đủ nguyên liệu + length-mix, build fail-closed, port 'Z' policy, find_descriptor == linear scan trên balls) · expect **53/53** · check **45/45** (p05 mở rộng: pre-corruption decompile → tái ghi blob 'x' → F5 lại = E2E cache invalidation) · matrix **24/24** · golden-merge strict `lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` bất biến · fuzz standalone **46.300.463 exec no-crash** (100k iter × 463 fixture) · format/tidy/dep xanh |
| asan-smoke | **PASS 58,75s khi máy nhàn** (chạy lại sau khi mọi sweep nền kết thúc). Lỗi FAIL 07:33/07:44 (×2, deterministic trong lúc đó) ở signature **ngoài wasmhex** — `global-buffer-overflow` READ 2B trong interceptor `strspn` ← `qstrtok` ← `bochs_user.dylib` ← `get_debugger_plugins` ← `open_database` — chỉ xuất hiện khi ≥2 tiến trình idalib chạy nền song song; discriminator `git stash` về HEAD thuần `e62e239` **vẫn fail** trong điều kiện đó ⇒ **không phải regress M2**, là can thiệp môi trường (biên bản dual-run `artifacts/m2_asan_smoke_probe.md`, nhóm K8) |
| Ghi chú | (i) 2 điểm single-decode cố ý giữ: verify `call_indirect` pc_f-loop cần Op2 (table index) không có trong stream record; emulate.cpp 'j' là processor-side lúc load (không phải F5). (ii) interactive re-F5 cùng hàm = LRU hit (đường fast nhất); sweep tuần tự mỗi hàm 1 lần = miss mỗi hàm (như thiết kế — tuần tự không tái F5). (iii) multi-IDB 1 tiến trình: closebase clear + identity khác nhau per IDB (bài học 3.7 áp dụng ngay từ thiết kế, không vá sau). |
| Còn mở | M4: G4 GoCallIR giá trị hóa · D4 chuyển nhận diện về IR · x=f apply · wb-collapse go-frame 142 · G9 global `g_<addr>` — M2 chỉ thay tầng nguồn đọc |

### T0 rev35 — M5 tái cấu trúc layout §9 (5 commit moves-only, 0 hàm đổi)

Ngày 15–16/11/2026 · HEAD `fab9fa1` ← `e90b278` ← `a672fbd` ← `6529ff4` ← `3dc1329` ← baseline `a14ec1e`.
Cơ chế restructure thuần: `git mv` + include-path fixes + sources.cmake/dep_check — T0 bump MỘT LẦN cuối đợt.

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `7d903250c6612ad17d4815063cd92ac0` — **KHÔNG ĐỔI so với trước M5** (engine byte-identical; bằng chứng mạnh nhất 0-dòng-logic-đổi) |
| `~/.idapro/loaders/wasmhexldr.dylib` md5 | `f884a06448b86dbf8ff050da29da1ae0` (đổi chỉ do TU loader reshuffle từ commit debuginfo/dwarf-split + dời file; output bất biến) |
| Cấu trúc hiện thực | `core/{binary,validation,analysis,debuginfo,component,ir,toolchains}` · `backend/hexrays/{microcode_emitter,ctree_presentation,register_mapping,host_adapter}` · `backend/idb/{metadata_schema,persistence}` · `ida/{loader,processor}` — hết `decompiler/`+`ida/*`+`loader/`+`processor/` cũ; 5 thư viện CMake SDK-free một chiều `binary ← validation ← analysis ← {debuginfo,component}` (dep_check phủ; backend/idb+hexrays biên dịch vào 2 addon target — dual-compile WASMHEX_NO_HEXRAYS, ghi trong commit) |
| Bảng 5 commit | `3dc1329` analysis/+ir/ (4 mv; 4-file split control_scan honest-decline — 1 hàm 1.656 dòng state dùng chung) · `6529ff4` debuginfo/ (pclntab + dwarf-split ~920 dòng SDK-free, loader giữ qf*+msg+merge; 2 cast-precedence tidy-fix giá trị-bất-biến) · `a672fbd` component/ · `e90b278` binary/ phần còn (parse_module+parse_code+reader.cpp; types.hpp còn thuần shim — 44 consumer) · `fab9fa1` T5 backend/+ida/ (D3/D4 đóng → di chuyển toàn bộ rewriter tree) |
| **Sweep đối chứng (runner idalib, install từng commit)** | duckdb sample 640 stride-99 **`0e464bfd…` BẤT BIẾN qua cả 5 commit** (== chuẩn GĐ2b) · balls **`36d4127a…`** · ballclone **`b3741370…`** · g126 **`604af852…`** — 3 corpus này BẤT BIẾN qua cả 5 commit + khớp chuẩn rev24→rev34; main126 full 3.255 hàm đo cuối đợt **`4d333ba8…` BẤT BIẾN** so baseline trước-M5 (5,7h/run — không lặp mỗi commit); refused=0, failures=0 mọi corpus |
| Bar mỗi commit | 3 configure xanh (SDK-free root + root super-build NATIVE=ON + native/wasmhex standalone) · core_tests **111/111** (103.886 asserts) · expect **53/53** · matrix **24/24** · check all **45/45** · golden-merge strict `lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` bất biến · format/tidy/dep xanh |
| fuzz standalone cuối đợt | **build xanh ngay tại commit 1** (configure+build `build/fuzz` cùng lúc verify nhóm analysis) + **926.463 exec no-crash trên cây cuối `fab9fa1`** (slice 2.000 iter × ~463 fixture, ASan+UBSan, 40,9s — chạy xong 16/11 sau khi 2 đợt 300k/100k bị môi trường SIGTERM/SIGKILL, log mất). Chốt theo chỉ thị Root: không chạy thêm; các commit giữa thuần `git mv` + include-fix (engine byte-identical, md5 procs không đổi) nên slice cuối đại diện toàn chuỗi. Bar 100k×fixture (rev34) chuyển mục còn đợt sau. |
| Phần còn M5 (đợt sau) | 4-file control_scan split (SPLIT_PLAN refactor + test từng file) · Go scans `core/parse_code.cpp` → `toolchains/go` · validation/{type_system,control_stack,definite_assignment} · `wasmhex_vt` khỏi types.hpp (DECISIONS #21) · T3 FunctionIndex/TypeIndex/GoStackOffset (chạm rộng, không moves-only) · ir/ đầy đủ |

### T0 rev36 — SLICE DRAW-TO-TARGET: thu hẹp output `draw` về đúng hình đích todo §2 (6 việc, 5 land + 1 decline đo mới)

Ngày 16/11/2026 · HEAD `4e13ca6` ← `305ed80` ← `8594dec` ← `c442a3f` ← `8a41457` ← `85153b6` ← `236d0c4` ← baseline `27728c8`.
Bốn khoảng trống display của `main.(*Ball).draw` đóng (pcb-drop / global name / param name / member real / empty-args inline); x=f retry đo đầy đủ chuỗi gate và **decline đứng nguyên với INTERR 52849 tái xuất trên cả 2 hàm áp được**.

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `9c0f566aa759a2b9c2ddd4591c7ab682` · loaders `fd3c462afae829458784fa6754bcea63` (CLT clang21 `build/d1`, deploy cuối phiên) |
| `236d0c4` go-sig-drop-pcb | Step C bỏ `int a1@<r0>` khi `pc_b_register_proof` (local 0 chỉ là PC_B/temp — mọi read đã consume ở lowering); window params thành toàn bộ chữ ký (`go__main_Ball *a1`). Kèm guard cache: seed `scan_wasm_function_type_cache` chỉ nhận param0 register-located — lần 1 guard sai (cả-stack).Measure: 812× MERR_BADARCH; guard đúng giữ nguyên mọi seed hỗn hợp r0+stack cũ. Corpus: 1.083 identical / 676 changed = 674 go-sig-drop-pcb (iso/reorder) + 8 `-lvar` jitter thứ cấp (đã liển kê từng file trong commit). G8 note mirror a1/a2 |
| `85153b6` go-global-name | Core walk `go_jsvalue_receiver_globals` (ir.cpp): mỗi call known-API js.Value, 2 store receiver (window 0/8) đều `i64.const A{,+8}; i32.wrap; i64.load a3 off0; local.set` → prove A. Loader: `g_<HEX>` + tinfo `go__js__Value` (16B item). Unit pin: đúng 1 global `0x150B90`. Corpus: 1.754/1.757 identical, 3 changed go-global-name (cast `(go__js__Value)` tự rơi nơi global đã typed) |
| `8a41457` go-param-name | known-API table thêm tên param Go (this/method/value/prop/args/format); `go_sig_param_t::name`; Step C áp tên khi pairing khớp byte. `syscall_js.Value.Set(go__js__Value this, go__string method, go__iface value)`; Length/Type a1→this. Corpus: 1.754/1.757 identical, 3 changed. **Honest limit**: callee có kết quả typed (Call/Get/Sprintf…) giữ sret (cùng bằng chứng INTERR 52849 — by-value struct return + raw-i32 call sites) |
| `c442a3f` go-member-real | Load member offset≠0 hạ qua typed receiver: G8 proven → wrap EA ghi `go_receiver_wrap_eas` → `emit_integer_conversion` giữ giá trị 8B (marker `is_go_receiver_address`) → `emit_fixed_memory` m_ldx qua pointer 8B + memarg gập addend 8B (base 0 giữ exact). Note chỉ bỏ khi til-struct phủ byte-range (nếu không giữ nhãn — `a1 + 32` vẫn có `// go: a1->prec`). draw: `len = a1->color.len; v2.tab = a1->color.ptr; y = a1->y; size = a1->size`. Corpus: 1.631 identical / 126 changed, 4 lớp (9 real · 42 flat+note · 55 flat-clean note gộp · 20 lvar signedness jitter) |
| `8594dec` go-call-inline | Cell-graph probe (unit pin): 17 call variadic, **4 all-zero []any** (mỗi slot `i64.const 0` store, verify per-EA trên decoded stream). collect prove `empty_variadic_args` fail-closed; apply consume 3 store 0; emit bỏ param (decl + arity cross-check + synthetic args). `syscall_js.Value.Call(g_150B90, "beginPath");` · `fmt.Sprintf(v2);` — memset biến mất. arc (5 phần tử) giữ đủ 3 tham số. Corpus: 1.755/1.757 identical, 2 changed go-call-inline (draw, main.main) |
| `305ed80` G5 x=f retry — **DECLINE đo mới** | Flip `kGoXfApplyEnabled`, đo từng gate trên balls: eligible **5** (Value.Get/Call/New/String + Sprintf — unit pin) → struct gate pass **2** (`go__string`: Value.String, Sprintf; 3 callee js.Value-result không descriptor struct) → Step C x=f decl parse + 'U' áp **thật** cho cả 2 → **CẢ HAI chết INTERR 52849** (`fmt.Sprintf @10016EB3F`, `syscall_js.Value.String @10015F8F3` — scattered-return mba vẫn drop m_ret). Switch trả false; 2 census test pin denominator cho route retvar/call-site-first sau này |
| Boxing-inline (`(any)b->x` tại arg) | **Decline cơ chế** (không đo thêm): mcall arg mop >8B chỉ biểu diễn qua stkvar — microcode không có struct-literal; boxed array 10 phần tử single-use vẫn render mảng + annotation (đủ giá trị, đúng như G7 ghi nhận engine limit) |
| **draw FINAL** (dump cuối, 1.759/1.759) | `void __usercall main.(*Ball).draw(go__main_Ball *a1)` · 4 lời gọi đúng thứ tự nguồn · beginPath/fill 2 tham số, arc 3 tham số + slice 5 phần tử + annotation args đủ 5 · `a1->color/x/y/size` render thật · `g_150B90` typed · Sprintf 1 tham số (x=f single-result vẫn nợ engine) — [dump đầy đủ trong commit `8594dec` discussion + `draw_final` của phiên] |
| **F5-subset** | balls **1.759/1.759 failures=0 `36d4127a…` trùng rev24→rev36** · ballclone **1.618/1.618 `b3741370…` trùng** · g126 **1.794/1.794 `604af852…` trùng** · duckdb sample stride-100 ×2 **634/634 `5f728cb0…` BẤT BIẾN** (bar chính DECISIONS #23, hidden+explicit cùng sha) · duckdb stride-99 **`0e464bfd…` BẤT BIẾN** (hidden+explicit cùng sha, == chuẩn GĐ2b/rev35 — chỉ Go đổi, runner-chuẩn không đụng) |
| Bar cuối phiên | core_tests **117/117** (103.929 asserts; +3 TEST_CASE: G9 walk pin, G4-lite census pin 17/4/13, G5 x=f eligibility pin 5) · expect **53/53** (không rebaseline — fixture Go không đổi) · check all **45/45** · matrix **24/24 red=0** · golden-merge strict `lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` bất biến · format/tidy/dep xanh · **asan-smoke PASS 38,11s** |
| Marker verify (strings dylib deployed) | `wasmhex: go frame step-C params: … pcb-drop=%zu` (loaders) · `wasmhex: go js.Value receiver globals: named=%zu skip=%zu` (loaders) |

### T0 rev37 — G4-full: single-use call-window stores folded into call expressions

Ngày 17/11/2026 · HEAD `a2fbd70` (G4-full) ← `ba9aca5` (rev36). Mảng draw-to-target khép lại: các cặp "chuẩn bị window rồi call đọc" của rev36 giờ được fold theo reaching definition trên cell-graph.

| Mục | Giá trị |
|---|---|
| `~/.idapro/procs/wasmhex.dylib` md5 | `cf2ff0d49fb8d96b925ae8833937c32b` · loaders `fd3c462afae829458784fa6754bcea63` (CLT clang21 `build/d1`, deploy cuối; lưu ý vận hành: build/asan ghi đè SDK bin — phải relink d1 loader trước deploy, đã dọn) |
| Cơ chế (collect_go_call_shapes) | Per 16B group param: cell-graph reaching-def — mọi window touch của [off, off+16) ngoài 2 slot stores của group phải nằm TRƯỚC store đầu hoặc SAU call (window outgoing dùng chung giữa các call — fill của call khác ngoài interval vẫn cho phép); escape địa chỉ cell trong interval fail; RET-push `[SP]=RESUME_PC` (plumbing đã consume) loại khỏi audit (không loại thì 1535/1559 group bị chặn — đo thực). Value shapes qua reaching-def trên local (setter CUỐI trước store — Go reload local mỗi call): const word, hoặc cặp load địa chỉ-hằng A/A+8 (`i64.const; wrap; i64.load off=0; local.set`) |
| Hai kiểu fold | (1) `fold_global_source`: arg = mop_v global trực tiếp — receiver `syscall_js.Value.Call(g_150B90, "beginPath")` với store receiver BIẾN MẤT hoàn toàn; (2) `fold_const_pair`: store header string consume, arg giữ cell chết + G6 literal swap hiển thị text. Side-effect đã đo: MỘT SỐ callee (họ runtime.throw) call-cast hiện thay vì bị propagation che — net dòng giảm, literal giữ |
| Không fold (fail-closed) | Group giá-trị-từ-biến (Sprintf format từ b->color; iface từ result; slice arc qua mảng boxing — declined struct-literal), group 24B, mixed (đo: 30 GLOBAL-FOLD + 1453 CONST-FOLD áp; skip src 44+38, cells 2) |
| **draw FINAL** | Mở hàm thẳng bằng `((void (__usercall *)(go__js__Value, go_string))syscall_js.Value.Call)(g_150B90, "beginPath");` — KHÔNG còn store window nào trước beginPath/fill; arc giữ slice 5 phần tử + annotation args; Set giữ 2 fill iface (giá trị từ biến); Sprintf giữ 2 fill string (từ b->color) — đúng các decline đã ghi |
| Corpus delta balls (vs rev36) | **1.248/1.757 identical, 509 changed** — machine-categorized họ go-window-fold: **356** fold thuần (chỉ store const/global biến mất) + **119** adjacent jitter (call-cast normalization, lvar signedness, copy-propagation fusion, decl dọn — mọi dòng diff khớp whitelist chữ ký đã review) + **34** fold + engine reshuffle sâu hơn (guard/label/return renumber): fmt.__pp_.printValue, internal_runtime_maps.{__unhashableTypeError_.Error, unhashableTypeError.Error}, main.main, main.main.func1, reflect.{ChanDir,Kind}.String, reflect.__ValueError_.Error, runtime.{SetFinalizer, __mspan_.reportZombies, __pageAlloc_.grow, __scavengerState_.sleep, __sysMemStat_.add, allgadd, exitsyscall, gcDumpObject, growslice, initMetrics, printpanics, procresize, throw, traceAdvance.func6, traceWriter.writeGoStatus, typeBitsBulkBarrier, typelinksinit}, sync.__Pool_.pinSlow, sync_atomic.CompareAndSwapInt32, syscall.{Errno.Error, Pwrite, Write, init}, syscall_js.{FuncOf, Value.String, ValueOf} |
| **F5-subset** | balls **1.759/1.759 failures=0 `36d4127a…` trùng rev24→rev37** · ballclone **1.618/1.618 `b3741370…` trùng** · g126 **1.794/1.794 `604af852…` trùng** · duckdb stride-100 ×2 **634/634 `5f728cb0…` BẤT BIẾN** (bar chính DECISIONS #23) · duckdb stride-99 ×2 **`0e464bfd…` BẤT BIẾN** (== chuẩn GĐ2b/rev35/rev36) |
| Bar cuối phiên | core_tests **117/117** (103.929 asserts) · expect **53/53** (không rebaseline) · check all **45/45** · matrix **24/24 red=0** · format/tidy/dep xanh · **asan-smoke PASS 56,46s** (code sau format) |

### T0 rev38 — K4 đầu tiên xử dụng: support-table hàng 2 cho IDA 9.5 build 261026 (hexx64 `e813cef8`) — 5 patch sites re-derived

Ngày 17/11/2026 · HEAD `91ae145` ← `81fa043` (rev37). User update IDA ngay sau phiên rev37: build **9.5.261026.365f1f33**, hexx64 SHA-256 `e813cef8f8785f73507abbfc95ccb1a837c36c84cb0f48494ee1a8d147361211` — bảng 1 hàng cũ từ chối đúng kiểu (thông báo rõ, disassembly sống, F5 tắt). Kịch bản support-table thật đầu tiên: re-derive 5 site trên binary mới, thêm hàng bảng — **KHÔNG đổi engine, mọi hash pin bất biến**.

| Mục | Giá trị |
|---|---|
| Cơ chế derivation | Bản cũ phục hồi từ /tmp patched copy (undo 5 byte; codesign chỉ đổi __LINKEDIT — __TEXT nguyên vẹn) → llvm-objdump 2 bản → pattern-match lõi `ldr/cmp/b.cond` chuẩn hóa target tương đối. Cả 5 site khớp ngữ nghĩa qua autorev (create_database + decompile hàm chứa): **expected bytes GIỐNG HỆT bản cũ — chỉ địa chỉ dịch** |
| 5 site mới (ngữ nghĩa, từ pseudocode hexx64 tự decompile) | (1) `0x30C7CC` b.ne — init gate #1 processor-ID (`*(v0[1]+4) != ph->id` → destroy module) trong sub_30C778 (module init, chuỗi "requires %s processor"); (2) `0x30C8A8` b.ne — gate #2 cùng so sánh, nhánh lỗi in "%s: requires %s processor (%s cannot handle it)" + có thể `qexit(1)`; (3) `0xEFE78` cbz — decompile-entry (sub_EFE00): `!arch_supported(ctx)` → **MERR_BADARCH (-31)** trước khi tạo mba (đúng comment "Nexus rejects custom processor IDs"); (4) `0x1CAD8` `movn w0,#-13` — khối fail frame-builder (sub_1C830) trả **MERR_BADFRAME (-13)** → thay `movz w0,#0`; (5) `0x182F8C` b.ls — `[ctx+0x590] > 1` → **MERR_COMPLEX (-22)** → thay `b +0x20` đích stp q0 (điểm rơi cấu trúc trùng patch cũ). Cũ→mới: `0x2FF788/0x2FF864/0xED2A4/0x1C798/0x17F170` |
| Refactor bảng | `host_support.{hpp,cpp}`: `host_patch_t {offset, expected[4], replacement[4]}`; mỗi entry mang `patches[]/patch_count` riêng (2 set không chia sẻ — offset build-specific); `match_host_support()` trả entry khớp; host.cpp áp `entry->patches` với guard fail-closed giữ nguyên; refusal message unknown_build liệt kê MỌI SHA của platform (2 hàng) |
| **F5-subset** | balls **1.759/1.759 failures=0 `36d4127a…` TRÙNG rev24→rev37 — KHÔNG re-baseline** (patch chỉ mở gate, engine output bất biến qua IDA patch level) · ballclone **1.618/1.618 `b3741370…` trùng** · g126 **1.794/1.794 `604af852…` trùng** · duckdb sample ×2 **634/634 `5f728cb0…` BẤT BIẾN** (bar chính DECISIONS #23, hidden+explicit cùng sha) |
| Bar cuối phiên | core_tests **120/120** (104.016 asserts; +3 TEST_CASE bảng 2 hàng: hàng 261026 pin byte-exact 5 site + 5 site cũ nguyên vẹn + well-formed {platform,sha} duy nhất/offset duy nhất/expected≠replacement) · expect **53/53** (không rebaseline) · check all **45/45** · matrix **24/24 red=0** · golden-merge strict `lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` bất biến · format/tidy/dep xanh · fuzz **KHÔNG chạy** (chỉ thị user) |
| Guard verify binary thật | Script đọc 5 offset trên `/Applications/IDA Professional 9.5.app/.../hexx64.dylib` (SHA `e813cef8…`): cả 5 `[OK] actual == expected` — bảng khớp binary tải về, guard sẽ pass |
| Draw check | `abi-probe --function "Ball).draw"`: decompile OK `lines=46 SET_PAIR=0 PAIR64=0 LODWORD=0 HIDWORD=0` |
| Deploy + marker | `~/.idapro/procs/wasmhex.dylib` md5 `2e6a06d1f8bee472a4d657bf5037a43c` · loaders `b1b0d1a654c4592213acdb527a1fa3f8` (CLT clang21 `build/d1`; lưu ý vận hành: cmake install bỏ LC_RPATH so bản copy SDK bin thẳng của rev37 `fd3c462a…` — cùng logic, nguồn không đổi git, toàn bar xác nhận; từ giờ ghi nhận md5 theo đường install chuẩn) · **marker strings dylib deployed: cả 2 SHA `88d5c6dd`/`e813cef8` hiện diện** (bảng 2 hàng biên dịch vào binary) |
| Ghi chú | Cơ chế từ chối đã được xác nhận trong thực tế trước khi land: với bảng cũ, IDA mới in thông báo đủ 2 SHA + actual + posture, F5 tắt sạch (KHÔNG crash, không patch mù) — đúng DECISIONS #15/#29-era fail-closed. INTERR 508xx-era không xuất hiện ở vòng này: các số đó thuộc verifier (helper `0x2D3938`-era cũ, `0x2E…` mới) và không cần chạm |

### T0 rev39 — XREFS: data references từ constant addresses proven + vá IDA 9.5.261026 reset bitness (điều tra H1/H2/H3 vụ drefs=0)

Ngày 17/11/2026 · HEAD `ce57e48` + `4dcbe0d` ← `fd263e2` (rev38). Task khẩn từ Root: "data xrefs (drefs) = 0 trên corpus thật sau khi user update IDA 9.5.261026; user báo xref function và data chưa ổn". Ba giả thuyết phân định bằng thí nghiệm A/B (idat + idalib fresh-load, script Root /private/tmp/xrefchk.py, IDAUSR riêng từng bên):

| Cấu hình | balls load | balls funcs/crefs | balls drefs | m0a drefs |
|---|---|---|---|---|
| pre-D1 `0047435` (build sạch, IDA mới) | OK (layout .text <4GiB) | 1759 / 337097 | **0** | 0 |
| HEAD `81fa043`-rev38 deploy `2e6a06d1` (không fix) | **FAIL** `bad segment start` → `Cannot create WASM code segment` → error 4 | — | — | — |
| + `4dcbe0d` (fix H2) | OK | 1759 / **337097** | 0 | 0 |
| + `ce57e48` (H3 drefs) | OK | 1759 / **337097** | **15** (g_150B90) | 0 |

| Kết luận | Bằng chứng |
|---|---|
| **H1 (regression D1) LOẠI** | pre-D1 trên IDA mới cũng drefs=0; D1 không tạo ra cũng không phá dref — nguồn dref chưa từng tồn tại |
| **H2 ĐÚNG, nặng hơn báo cáo** | IDA 9.5.261026 (`libida.dylib` 17/11 11:26 đi cùng license update): `set_processor_type(SETPROC_LOADER)` **reset app bitness về PR_DEFSEG32 (32)**, ghi đè `inf_set_app_bitness(64)` của loader → mọi module có memory (D1 .text >4GiB) fail load TOÀN BỘ. Probe trong loader in bitness=32 ngay sau set_processor_type (kỳ vọng 64). Vá `4dcbe0d`: re-apply D1 bitness rule một nguồn (memory_mode) sau khi processor load — idempotent, no-op trên build cũ |
| **H3 (feature thiếu) ĐÚNG** | `git grep add_dref` qua `0047435`/`d09bfcc`/`de007af`/`81fa043`: **0 hit** — toàn bộ xrefs là add_cref (emulator). dref=1 trên m0a lúc Root đo 12:23 là noise auto-analysis (fresh-load lại sau fix: 0) |
| Dữ kiện 3 (bt_large không in XREFCHK) | Log `/private/tmp/xrefchk2.log`: "Can't find input file" — path sai (`tests/fixtures/bt_large.wasm` không tồn tại; file thật `tests/fixtures/brtable/bt_large.wasm` load OK: funcs=1 drefs=0 — fixture br_table thuần, không const access) |
| Khe chưa giải thích | 2 chạy của Root 12:23-12:24 trên cùng binary+plugin load OK (log xrefchk{,3}.log đầy đủ funcs/crefs); mọi chạy từ 12:28 (của subagent này) đều fail cho tới fix — tái lập trên cả env sạch, IDAUSR trắng, plugin rebuild từ git; không dựng lại được trạng thái OK; theo dõi nếu tái diễn |

| Mục | Giá trị |
|---|---|
| Feature (3 lớp fail-closed) | (1) `core/analysis/mem_drefs.cpp` (SDK-free, lib wasmhex_analysis): abstract-stack maybe-const walk — push const 0x41/0x42; fold đúng arity i32/i64 add/sub + wrap/extend; scalar load/store pop đúng shape (effective = operand + memarg); **mọi opcode không model arity → clear stack** (không đoán); global.get emit record tham chiếu (immediate index — proven bởi opcode). Ghi nhớ kiến trúc: `module.code` là stream ĐÃ REWRITE, `body.offset` trỏ thẳng opcode đầu (parse_code copy từ sau local decls) — walker không skip locals. (2) Loader: lưu 1-record-per-EA netnode 'R'(target ea_t) + 'r'(kind 1=R/2=W/3=global); memory target phải trong extent MEMORY; snap `get_item_head` để cả 2 word của cặp js.Value 0x150B90/98 đếm cho g_150B90. (3) Emulator áp `add_dref` khi instruction ĐÃ decode — add_dref cần defined item tại from; thử áp ở loader đo `no defined item at from` 22k/22k (auto-analysis decode sau load) — cùng lý do crefs phát từ emulate_instruction |
| Đo balls fresh-load sau land | `funcs=1759 with_callers=1755 crefs=337097` (byte-identical Root 12:23) · drefs: **g_150B90=15** từ draw/loop (đích đo Root) · loader summary `const-address data references: memory=2903 globals=19510` (phần lớn đến items auto-named/unamed trong .globals/MEMORY — naming là việc presentation, ghi follow-up) |
| **F5-subset BẤT BIẾN** | balls stride-1 **`36d4127aaa92aeef4d3db1fa4fa6997642b4af1479d0790a8b9cac503e2fd7ef`** trùng rev24→rev38 · duckdb stride-100 ×1 **`5f728cb0…`** trùng (bar chính DECISIONS #23) — dref là metadata IDB, không chạm pseudocode/lowers |
| Bar cuối phiên | core_tests **122/122** (rev38 có 120; +2 TEST_CASE H3: fixture h3_dref_walk 2 const proven + 2 dynamic từ chối + 2 global.get record; balls receiver-word 0x150B90 proven + có store + globals>100) · spec_oracle.tsv +1 row, pin checked 417→418 · format-check + tidy-check sạch · dep_check OK ("binary/validation/analysis SDK-free one-directional") · fuzz KHÔNG chạy (chỉ thị Root) · KHÔNG đụng `backend/hexrays/host_adapter/*` (worker hexx64 giữ) |
| Deploy + marker | `~/.idapro/procs/wasmhex.dylib` md5 `03f811f4a18b66689d529fe4e377567a` · loaders `f7eec665d0eccc18cabfbc07241da2a4` (CLT `/usr/bin/c++` qua `build/dx` — dir riêng không đụng build/d1 của worker; dylib copy thẳng từ SDK bin như quy ước rev37) · marker `WASMHEX_BUILD_MARKER_M6_S1_SDK940_NODEPRECATED` + cả 2 SHA `88d5c6dd`/`e813cef8` (bảng rev38) hiện diện trong binary deployed |
| Vận hành | idat headless hiện fail "Failed to initialize IDA as library (error code 4)" NGAY CẢ KHI load — cùng diagnostic với lỗi loader; idalib (`idapro` python 3.13, `/Users/tiendoan/env/bin/python`) chạy ổn định — mọi đo fresh-load phiên này qua idalib. Nếu idat cần dùng lại: điều tra riêng (đã loại: env, IDAUSR state, license, plugin bytes) |

### T0 rev40 — DREF TAG SPLIT 'R'/'r' → '8'/'9' + global target supval: khôi phục 43/102 hàm bị từ chối -31 trên corpus Emscripten mới (musl-00050c6a)

Ngày 17/11/2026 · HEAD `1703133` + `4cbbd5f` + `678f7fe`. Task khẩn từ Root: module mới `wasm-test/00050c6a.wasm` (Emscripten/musl 82KB, CTF, 102 hàm) có **43/102 hàm F5 REFUSED toàn -31** — trong đó `main/check_password/show_flag/hex_to_bytes/onclick/onkeydown`; corpus chuẩn vẫn "xanh" cùng binary.

| Chẩn đoán | Bằng chứng |
|---|---|
| Kênh bắt msg() | msg() idalib bị nuốt (0 dòng WASMHEX_F5_UNSUPPORTED trên stdout); chạy runner với `IDALOG=<file>` lộ toàn bộ: `WASMHEX_TYPED_CALL_UNSUPPORTED: declaration "void __usercall \xff\xff\xff(void);"` + `microcode lowering failed (itype=33=global_get, merror=-31)` tại MỖI entry hàm bị từ chối |
| Nhóm từ chối = 1 lớp duy nhất | 43/43 hàm bị từ chối có instruction ĐẦU TIÊN là `global.get` của global ĐỊNH NGHĨA (phân tích first-opcode 43×0x23/102; probe netnode idalib: mọi entry mang 'R' blob 8-byte le64=0xFFFFFF) |
| Cơ chế gốc | Slice H3 rev39 lưu dref 1-record-per-EA tag **'R'**(target)+**'r'**(kind) — 'R' đồng thời là slot CHUỖI startup-helper đọc ở entry hàm (filter.cpp `supstr_ea(entry,'R')`). `global.get` ở offset 0 → blob ea_t đè đúng entry → tên helper rác `\xff\xff\xff` → `parse_decl` fail → MERR_BADARCH -31 fail-closed (đúng thiết kế fail-closed — chỉ là dữ liệu vào sai) |
| Lỗi thứ hai cùng slice | target dref global.get đọc `eaget_idx(idx,'j')` = accessor NETMAP_VAL-flavor trên blob ghi bằng supset THÔ → trả **stored−1** (đo: global @0x1000000 → 0xFFFFFF; balls @0x270000 → 0x26FFFF) — xref global lệch 1 byte |
| Corpus rev39 đã gãy SILENT | Đo lại rev39-loader trên balls: **refused=1741/1759, F5-subset `9d35361a…` ≠ pin `36d4127a…`** — dòng freeze rev39 ghi "trùng" là đo lệch (không chạy lại sweep sau khi land dref); ballclone/g126/duckdb/main126 cùng shape (toàn bộ Go corpus ~99% hàm bắt đầu bằng global.get 0) |

| Vá | Nội dung |
|---|---|
| `4cbbd5f` | dref records chuyển tag riêng **'8'**(target ea_t)+**'9'**(kind uchar) — mọi chữ cái A–Z/a–z đã bị chiếm (kiểm kê registry + grep netnode calls; 'V' control-labels, 'P' param-vectors, 'l'/'t'/'b' blobs); ghi registry `dref_target`/`dref_kind` + cảnh báo. 'F' easet/eaget pair giữ nguyên (round-trip đúng) |
| `678f7fe` | target global.get đọc bằng `supval(idx,'j')` cùng flavor với ghi — đích = địa chỉ item chính xác |

| Đo sau vá (runner idalib, install ~/.idapro từ SDK bin) | Kết quả |
|---|---|
| **musl-00050c6a** (102 hàm) | REFUSED **43 → 0**, `WASMHEX_F5_DONE total=102 failures=0`, F5-subset mới **`b5a7fbeb2ae86c6482a48ac7f01252b7879113887a17cf9ef0852028b9076a21`** (pin corpus #6) |
| balls full 1.759 | refused 1741 → 0 · **`36d4127aaa92aeef4d3db1fa4fa6997642b4af1479d0790a8b9cac503e2fd7ef` TRÙNG pin rev24→rev38** (vá khôi phục pin, không đổi output engine) |
| ballclone full 1.618 | refused 0 · **`b3741370775ccab3f10f2654f7f7458dbedc29047610abb451268f6b976c2c34` trùng pin** |
| gowasm126 full 1.794 | refused 0 · **`604af8521847d49cfdf015d52f6ea7dde2c6af4f3c3096a5052c4492e94a7af5` trùng pin** |
| duckdb stride-100 634 hàm | refused 0 · **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` TRÙNG bar chính DECISIONS #23** |
| main126 full 3.255 | refused 0 · **`4d333ba8ae9284b9d3c0367aef0db3ab5fd07f788a04016105da286bd52e3901` TRÙNG pin trước-M5** (sweep + sha hoàn tất; rc=1 chỉ ở teardown cuối — work-dir scratch bị `rm -rf` dọn giữa chừng làm mất .til lúc close, không chạm kết quả đo) |
| Probe netnode post-fix | 'R' rỗng ở mọi entry (R_len=-1); '8' = 0x1000000 kind=3 (đích CHÍNH XÁC global 0) |
| Deploy + marker | `~/.idapro/procs/wasmhex.dylib` md5 `c4ce4f28f34810d1c2b8c86043d8c6b9` · loaders `176ff830b3927888841c5fd90a558ace` (build/d1 qua lock, dylib copy thẳng từ SDK bin) · marker mới `WASMHEX_BUILD_MARKER_T0_REV40_DREF_TAGS` hiện diện trong wasmhexldr deployed (cùng M6_S1 cũ) |
| Bar cuối phiên | core_tests **122/122** · expect **53/53** · matrix brtable **24/24** · `check all` **46/46** (+1 case mới `dref_entry_globalget`: FAIL trên rev39, PASS sau vá — fixture `wasmhex_dref_entry_globalget.wat` hàm đầu = global.get defined global, assert thêm đích xref == supval('j')) · format-check + tidy-check + dep_check sạch · corpus-check 6/6 (manifest +1 dòng `musl-00050c6a`) · fuzz KHÔNG chạy (chỉ thị Root) · KHÔNG đụng host_adapter |

### T0 rev41 — C STRING ARGS: đối số hằng ở vị trí con trỏ → &item / string literal (P0-1 proof extension)

Ngày 17/11/2026 · HEAD `1d4c385` + slice này. Task khẩn từ Root (user: "fix string nữa" trên corpus #6): hằng đối số tại vị trí tham số CON TRỎ hiển thị địa chỉ/literal thay vì số — `sscanf(v3, 1348, v2)` phải thành `sscanf(v3, "%2hhx", v2)`.

**Nguyên tắc P0-1 GIỮ NGUYÊN** — hằng làm GIÁ TRỊ vẫn numeric; chỉ MỞ đường chứng minh: vị trí i của callee có kiểu con trỏ + hằng resolve vào [span_begin, memory extent) → hiển thị. Ba nguồn proof vị trí con trỏ (fail-closed từng điều kiện):
1. **Bảng libc nhỏ** (`core/analysis/param_pointers.cpp`) — CHỈ vị trí pointer của printf/scanf family + memcpy/strlen/strcpy/strcmp/... (sscanf {0,1}, snprintf {0,2}, ...); không đoán kiểu.
2. **Prototype callee** (tinfo tại entry, arity-matched — sret không thể lệch vị trí; nguồn M4-S1/standard_import_prototypes).
3. **Body-proof walk** (`param_pointer_walk`, blob '4' + blob '3' per call-EA + dref kind-4 '8'/'9'): param được chứng minh dùng làm địa chỉ trong thân callee (load/store base qua local/spill-cell sp-like, hoặc truyền vào vị trí pointer của callee khác — fixpoint ≤4 pass, cap stack 256, unmodeled opcode clear stack). Spill-cell keyed (base local sp-like + offset), kill khi base được redefine; locals union-of-defs (may), sp_like AND-of-defs.

**Render** (kênh Go G6 tái dùng): CMAT_FINAL swap — chuỗi NUL-terminated printable (≤1MB, escape `\" \\ \n \r \t \xNN` như `go_escape_c_literal`) → `make_go_string_literal`; BSS/binary → `make_data_address_literal` (cot_ref trên cot_obj, item tồn tại nhờ dref kind-4 — không mutate IDB từ decompiler). Constant qua local (`i32.const; local.set; local.get; call`) không còn mang const-ness trên operand → dùng record blob '3' của loader (cùng instruction stream).

**Ngưỡng quan trọng (đo từ sai lệch đầu)**: hằng phải ≥ span_begin — cho phép item ở [0, span_begin) làm engine re-anchor TOÀN BỘ hiển thị sp-relative khác (đo thực: 52/102 hàm corpus #6 churn chỉ vì item mới ở 0x28); siết lại còn đúng 5 hàm đích.

| Corpus | Trước | Sau | Phân loại |
|---|---|---|---|
| musl-00050c6a (102 hàm, dump goldens) | 5 hàm chứa triệu chứng | **5 hàm đổi, 97 byte-identical** — `check_password` (MD5_Init/Update `&unk_BC4`, MD5_Final `&unk_C60,&unk_BC4`, hex_to_bytes `"936fff76f378ace4cf83a4360cafcc20"`, strcmp `&unk_C60`), `show_flag` (snprintf `"Good job! Your flag is: %s"`), `hex_to_bytes` (sscanf `"%2hhx"`), `fmt_fp` (out `"."` ×2 — body-proof), `onkeydown` (strcmp `"Enter"`) | lớp `c-string-arg` (6 literal) + `c-ptr-arg` (6 &unk) |
| duckdb stride-100 634 | `5f728cb0…` | **`5f728cb09fad0128f2b03ff1643915c2b5a74e817979ebb7669fbe79a2df2fbd` BẤT BIẾN** (passive-data → walk skip toàn bộ; 634/634 OK) | 0 hit — đúng theo construction |
| balls / g126 / ballclone / main126 (dump goldens, old-vs-new từng hàm) | 8.422 hàm baseline rev40 | **0 hàm đổi — byte-identical 100%** (balls 1757, g126 1792, ballclone 1618, main126 3255) | 0 hit — Go-shaped call sites skip theo thiết kế; walk không chứng minh được vị trí nào trên Go |
| balls / ballclone / g126 / main126 F5-subset FULL | pins rev24–rev40 | balls `36d4127a…` · ballclone `b3741370…` · g126 `604af852…` · main126 `4d333ba8…` — **TRÙNG TUYỆT ĐỐI cả 4** (failures=0 toàn bộ) | bất biến |

| Bar | Kết quả |
|---|---|
| core_tests | **125/125** (+3 TEST_CASE param_pointers: bảng libc, spill/forward shape pin đủ 5 record + mask đúng, memory_limit=0 không record; spec_oracle unlisted 30→31) |
| expect | **55/55** (54 + fixture `wasmhex_c_string_args` 8 expectation + pin `musl_00050c6a_cstrings` 12 expectation — tách: 53 cũ + 2 mới) |
| matrix / check | 24/24 red=0 · 46/46 |
| F5-subset corpus #6 | `b5a7fbeb…` GIỮ (hash chỉ phủ status; display đổi pin bằng golden fixture `musl_00050c6a_cstrings`) |
| golden-merge strict | `WASMHEX_GOLDEN_OK lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` (duckdb mergeProcessed — feature inert trên passive-data) |
| format/tidy/dep | format-check PASS (clang-format 23.1.5 đã rewrite + re-check) · tidy-check PASS (0 warning user code) · wasmhex_dep_check OK |
| Deploy | procs md5 `9cfa3c7f27f6055094343caa401e9cb3` · loaders `a6c47135a2a53a451dbea266354dbe2e` · marker `WASMHEX_BUILD_MARKER_C_STRING_ARGS` (wasmhexldr, strings=1) + `WASMHEX_BUILD_MARKER_C_STRING_ARGS_SWAP` (wasmhex procs) hiện diện trên dylib deployed · /tmp scratch runner dọn sạch |

### T0 rev42 — STRING LITERAL ANCHOR: neo tại data EA thật, không bao giờ tại call-site (strings-window pollution)

Ngày 17/11/2026 · HEAD `887c2bc` + slice này. Task khẩn từ Root (user mở Strings window trên `00050c6a.wasm`): item string "Decompiler" được tạo trong `.text` tại EA call-site thay vì tại data EA thật — `.text:0000000100005140 Decompiler 936fff76f378ace4cf83a4360cafcc20`, `.text:00000001000052CB Decompiler "Good job! Your flag is: %s"` (trong khi `MEMORY:00000544 C %2hhx`, `MEMORY:00000A90 C 936fff…` là item thật của loader).

**Cơ chế (chẩn đoán bằng SDK + tái hiện idat)**: Hex-Rays persist MỖI string literal của ctree cuối vào **registry synthetic-string của kernel** (`synth_string_set`, provider `SYNTHSTR_PROVIDER_DECOMPILER=1`; `strlist.hpp` `string_info_ex_t::decompiler_string`, type `STRTYPE_DECOMP=0x10`) tại **EA của cot_str** — Strings window liệt kê các entry này. Cả 3 kênh tạo cot_str của mình đều neo tại EA lệnh: (1) Go G6 `go_arg_literal_swap_t` và (2) C string args `c_arg_swap_visitor_t` đều gọi `make_go_string_literal(want.text, argument->ea)` (filter.cpp — EA của biểu thức tham số trong call, nằm trong `.text`); (3) `rewrite_memory_base_string` (rewriter_idiom.cpp) gán `literal->ea = expression->ea`. Data EA thật CÓ SẴN tại lúc record (G6: `string_ptr`; C-args: `address` đã resolve vào linear memory; membase: `data_base + offset`) — chỉ cần mang qua.

**Vá**: literal neo tại **data EA** (hash → 0xA90 nơi strlit item của loader đã tồn tại — tham chiếu item có sẵn, KHÔNG tạo item mới, không `create_strlit` ở đâu cả — builder thuần display). `go_arg_literal_t`/`c_arg_swap_t` thêm trường `data_ea` (record tại chỗ chứng minh); `record_c_arg_string_literal` thêm param `data_ea` (BADADDR → fail-closed giữ numeric); `memory_base_string` xuất `at_out`. Hợp đồng anchor ghi tại `make_go_string_literal` (ctree_types.cpp/hpp).

**Tái hiện trước/sau vá (idat + python scratch, staging idausr cách ly)**: probe đếm synthetic-string provider=1 qua `synth_string_get_range` trên từng segment, sau khi F5 toàn bộ hàm:
- corpus #6 (102 hàm F5): **trước 6 entry `.text`** (`%2hhx`@100005064, hash MD5@100005140, `Good job!...`@1000052cb, `Enter`@1000053db, `.`@100007649/100007768) → **sau 0** (không entry nào ở bất kỳ segment nào — hexarc tự dedup khi EA đã có strlit item thật, nên MEMORY cũng không thêm hàng mới).
- balls (200 hàm đầu F5): **trước 46 entry `.text`** (Go runtime: `concurrent map read and map write`, `GODEBUG: value "...`, …) → **sau 0**.
- duckdb (40 hàm nhỏ nhất): 0 trước lẫn sau (kênh rewriter không khai hỏa trên corpus này).
- **IDB cũ**: synthetic-string được lưu trong database, nhưng module wasm luôn load fresh (IDB mới mỗi lần mở) — fresh load + binary mới = 0 entry `.text` (verify trên 102 hàm); không cần migration.

| Corpus | Trước vá | Sau vá | Phân loại |
|---|---|---|---|
| musl-00050c6a dump-golden cũ-mới (102 hàm, binary rev41 deployed vs binary này) | — | **102/102 .c byte-identical** (chỉ `_metrics.json` lệch timing) | display KHÔNG đổi — literal vẫn hiện `hex_to_bytes("936fff…")` v.v., chỉ đổi neo |
| balls 1757 + ballclone 1618 dump cũ-mới | — | **0 hàm đổi — byte-identical 100%** | kênh G6 cùng kết luận |
| g126 1792 + main126 3255 dump cũ-mới | — | **0 hàm đổi — byte-identical** (main126 3255/3255 ok, interr=0 cả hai phía) | kênh G6 |
| F5-subset 6 corpus | pins rev24–rev41 | corpus #6 `b5a7fbeb…` · duckdb stride-100 634 `5f728cb0…` · balls `36d4127a…` · ballclone `b3741370…` · g126 `604af852…` · main126 full 3255 `4d333ba8…` — **TRÙNG TUYỆT ĐỐI cả 6, failures=0 toàn bộ** | bất biến |

| Bar | Kết quả |
|---|---|
| core_tests | **125/125** (104.060 asserts) |
| expect | **55/55** (kể cả `musl_00050c6a_cstrings` 12 + `wasmhex_c_string_args` 8 — display pin giữ nguyên) |
| matrix / check all / golden-merge strict | 24/24 red=0 · 46/46 · `WASMHEX_GOLDEN_OK lines=273 stack=0 helpers=3 coerce=0 goto=8 lvars=71` |
| corpus-check | 6/6 (ctest) |
| format/tidy/dep | format-check PASS (clang-format đã rewrite + re-check) · tidy-check PASS · wasmhex_dep_check OK |
| Deploy | procs md5 `e9d59199d0efacf1ff24a1c717a936b1` · loaders `a6c47135a2a53a451dbea266354dbe2e` (build/d1 qua lock) · marker mới `WASMHEX_BUILD_MARKER_STRING_LITERAL_DATA_ANCHOR` hiện diện trên dylib deployed · scratch runner_work dọn sạch |
