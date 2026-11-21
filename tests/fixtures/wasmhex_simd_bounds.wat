(module
  (memory 1)
  (func (export "write_seed")
    i32.const 0
    i32.const 0x12345678
    i32.store)
  (func (export "read_word") (param i32) (result i32)
    local.get 0
    i32.load)
  (func (export "store_end")
    i32.const 65520
    i32.const 0
    v128.load
    v128.store)
  (func (export "copy") (param i32 i32)
    local.get 0
    local.get 1
    v128.load
    v128.store)
  (func (export "conditional_copy") (param i32 i32 i32)
    local.get 0
    if
      local.get 1
      local.get 2
      v128.load
      v128.store
    end)
  (func (export "bounds_paths") (param i32)
    local.get 0
    if
      i32.const 0
      i32.const 65521
      v128.load
      v128.store
    else
      i32.const 65520
      i32.const 0
      v128.load
      v128.store
    end)
  (func (export "load_oob")
    i32.const 0
    i32.const 65521
    v128.load
    v128.store)
  (func (export "load_address_overflow")
    i32.const 0
    i32.const -16
    v128.load offset=32
    v128.store)
  (func (export "store_oob")
    i32.const 65521
    i32.const 0
    v128.load
    v128.store)
  (func (export "store_address_overflow")
    i32.const -16
    i32.const 0
    v128.load
    v128.store offset=32))
