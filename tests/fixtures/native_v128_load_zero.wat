(module
  (memory (export "memory") 1)

  ;; Copy the zero-extended 32-bit load into a full v128 store.
  (func (export "copy_load32_zero") (param $dst i32) (param $src i32)
    local.get $dst
    local.get $src
    v128.load32_zero offset=12 align=4
    v128.store offset=4 align=16)

  ;; Copy the zero-extended 64-bit load into a full v128 store.
  (func (export "copy_load64_zero") (param $dst i32) (param $src i32)
    local.get $dst
    local.get $src
    v128.load64_zero offset=24 align=8
    v128.store offset=8 align=16)
)
