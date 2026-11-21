;; M0/3.3 (todo 3.3): memory.fill / memory.copy must render through the SAME
;; linear-memory representation as the surrounding loads/stores (`base +
;; addr` on a wide database), so Hex-Rays sees one memory region per address
;; and its alias analysis holds. The old form printed the raw wasm32 offset
;; (`memset(v + 16, 0, 16)`) next to base-relative stores in the same
;; function.
(module
  (memory 1 2)

  ;; dynamic window: fill then load the same bytes — both must display
  ;; through the linear-memory base.
  (func (export "fill_load_same_window") (param $dst i32) (result i64)
    local.get $dst
    i32.const 0x5A
    i32.const 16
    memory.fill
    local.get $dst
    i64.load offset=8)

  ;; interleave a store into the window so the aliasing pair is visible:
  ;; store, fill over it, load back.
  (func (export "store_fill_load") (param $dst i32) (result i64)
    (local $v i64)
    local.get $dst
    i64.const 0x1122334455667788
    i64.store offset=0
    local.get $dst
    i32.const 0
    i32.const 64
    memory.fill
    local.get $dst
    i64.load)

  ;; copy form: memmove must use the same representation too.
  (func (export "copy_load") (param $dst i32) (param $src i32) (result i64)
    local.get $dst
    local.get $src
    i32.const 32
    memory.copy
    local.get $dst
    i64.load offset=24)

  ;; constant destination: the fill address resolves like a constant load.
  (func (export "const_fill_load") (result i64)
    i32.const 4096
    i32.const 0x33
    i32.const 64
    memory.fill
    i32.const 4104
    i64.load)
)
