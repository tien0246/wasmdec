(module
  (func (export "br_copy") (param i32) (result i32)
    block (result v128)
      i32.const 99
      local.get 0
      i32x4.splat
      br 0
      unreachable
    end
    i32x4.extract_lane 2)
  (func (export "br_if_copy") (param i32 i32) (result i32)
    block (result v128)
      i32.const 99
      local.get 0
      i32x4.splat
      local.get 1
      br_if 0
      drop
      drop
      v128.const i32x4 0x11111111 0x22222222 0x33333333 0x44444444
    end
    i32x4.extract_lane 2)
  (func (export "br_table_copy") (param $selector i32) (param $value i32) (result i32)
    (local $prefix i32) (local $payload v128)
    (block $outer (result v128)
      i32.const 55
      local.set $prefix
      local.get $prefix
      (block $inner (result v128)
        local.get $value
        i32x4.splat
        local.get $selector
        br_table $inner $outer
      )
      local.set $payload
      drop
      local.get $payload
    )
    i32x4.extract_lane 2))
