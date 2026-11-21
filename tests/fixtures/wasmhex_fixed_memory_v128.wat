(module
  (memory 1)
  (func (export "load") (param i32) (result i32)
    local.get 0
    v128.load
    drop
    i32.const 42)
  (func (export "load_last") (param i32) (result i32)
    local.get 0
    v128.load offset=65520
    drop
    i32.const 42)
  (func (export "load_max_offset") (param i32) (result i32)
    local.get 0
    v128.load offset=4294967295
    drop
    i32.const 42)
  (func (export "conditional_load") (param i32 i32) (result i32)
    local.get 1
    if
      local.get 0
      v128.load
      drop
    end
    i32.const 42)
  (func (export "switch_load") (param i32 i32) (result i32)
    block $exit
      block $skip
        block $load
          local.get 1
          br_table $load $skip $exit
        end
        local.get 0
        v128.load
        drop
      end
    end
    i32.const 42)
  (func (export "copy") (param i32 i32)
    local.get 0
    local.get 1
    v128.load
    v128.store)
  (func (export "load8_splat") (param i32) (result i32)
    i32.const 0
    local.get 0
    v128.load8_splat offset=1 align=1
    v128.store
    i32.const 0
    i64.load
    i64.const 0x5a5a5a5a5a5a5a5a
    i64.eq
    i32.const 8
    i64.load
    i64.const 0x5a5a5a5a5a5a5a5a
    i64.eq
    i32.and)
  (func (export "load16_splat") (param i32) (result i32)
    i32.const 0
    local.get 0
    v128.load16_splat offset=2 align=2
    v128.store
    i32.const 0
    i64.load
    i64.const 0x1234123412341234
    i64.eq
    i32.const 8
    i64.load
    i64.const 0x1234123412341234
    i64.eq
    i32.and)
  (func (export "load32_splat") (param i32) (result i32)
    i32.const 0
    local.get 0
    v128.load32_splat offset=4 align=4
    v128.store
    i32.const 0
    i64.load
    i64.const 0x1234567812345678
    i64.eq
    i32.const 8
    i64.load
    i64.const 0x1234567812345678
    i64.eq
    i32.and)
  (func (export "load64_splat") (param i32) (result i32)
    i32.const 0
    local.get 0
    v128.load64_splat offset=8 align=8
    v128.store
    i32.const 0
    i64.load
    i64.const 0x123456789abcdef0
    i64.eq
    i32.const 8
    i64.load
    i64.const 0x123456789abcdef0
    i64.eq
    i32.and)
  (func (export "load32_zero") (param i32) (result i32)
    i32.const 0
    local.get 0
    v128.load32_zero offset=16 align=4
    v128.store
    i32.const 0
    i64.load
    i64.const 0x12345678
    i64.eq
    i32.const 8
    i64.load
    i64.eqz
    i32.and)
  (func (export "load64_zero") (param i32) (result i32)
    i32.const 0
    local.get 0
    v128.load64_zero offset=24 align=8
    v128.store
    i32.const 0
    i64.load
    i64.const 0x123456789abcdef0
    i64.eq
    i32.const 8
    i64.load
    i64.eqz
    i32.and)
  (func (export "init")
    i32.const 129
    i32.const 0x5a
    i32.store8
    i32.const 130
    i32.const 0x1234
    i32.store16
    i32.const 132
    i32.const 0x12345678
    i32.store
    i32.const 136
    i64.const 0x123456789abcdef0
    i64.store
    i32.const 144
    i32.const 0x12345678
    i32.store
    i32.const 152
    i64.const 0x123456789abcdef0
    i64.store))
