(module
  (memory 1)
  (func (export "load8x8_s") (param $address i32) (result i32)
    i32.const 512
    local.get $address
    v128.load8x8_s offset=1 align=1
    v128.store
    i32.const 512 i64.load i64.const 0xffff007f0001ff80 i64.eq
    i32.const 520 i64.load i64.const 0xff80000000120034 i64.eq
    i32.and)
  (func (export "load8x8_u") (param $address i32) (result i32)
    i32.const 512
    local.get $address
    v128.load8x8_u offset=1 align=1
    v128.store
    i32.const 512 i64.load i64.const 0x00ff007f00010080 i64.eq
    i32.const 520 i64.load i64.const 0x0080000000120034 i64.eq
    i32.and)
  (func (export "load16x4_s") (param $address i32) (result i32)
    i32.const 512
    local.get $address
    v128.load16x4_s offset=1 align=1
    v128.store
    i32.const 512 i64.load i64.const 0xffffff7f00000180 i64.eq
    i32.const 520 i64.load i64.const 0xffff800000001234 i64.eq
    i32.and)
  (func (export "load16x4_u") (param $address i32) (result i32)
    i32.const 512
    local.get $address
    v128.load16x4_u offset=1 align=1
    v128.store
    i32.const 512 i64.load i64.const 0x0000ff7f00000180 i64.eq
    i32.const 520 i64.load i64.const 0x0000800000001234 i64.eq
    i32.and)
  (func (export "load32x2_s") (param $address i32) (result i32)
    i32.const 512
    local.get $address
    v128.load32x2_s offset=1 align=1
    v128.store
    i32.const 512 i64.load i64.const 0xffffffffff7f0180 i64.eq
    i32.const 520 i64.load i64.const 0xffffffff80001234 i64.eq
    i32.and)
  (func (export "load32x2_u") (param $address i32) (result i32)
    i32.const 512
    local.get $address
    v128.load32x2_u offset=1 align=1
    v128.store
    i32.const 512 i64.load i64.const 0x00000000ff7f0180 i64.eq
    i32.const 520 i64.load i64.const 0x0000000080001234 i64.eq
    i32.and)
  (func (export "init")
    i32.const 129
    i32.const 0xff7f0180
    i32.store
    i32.const 133
    i32.const 0x80001234
    i32.store))
