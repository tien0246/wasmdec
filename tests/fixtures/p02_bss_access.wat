;; P0-2 fixture: store/load of a variable at an offset beyond every active
;; data segment (BSS of the linear memory). Before P0-2 such addresses
;; resolved to nothing and printed as .text labels (`*(_DWORD *)&loc_…`);
;; they must now resolve into the single linear-memory segment at
;; 0x100000000 (wasmhex_memory_base).
(module
  (memory 2)
  (data (i32.const 16) "x\00")
  (func (export "bss_read") (result i32)
    i32.const 130048
    i32.load)
  (func (export "bss_write") (param i32)
    i32.const 130048
    local.get 0
    i32.store))
