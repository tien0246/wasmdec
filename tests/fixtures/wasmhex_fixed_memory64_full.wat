(module
  (memory i64 281474976710656)
  (func (export "last_byte") (param $a i64) (result i32)
    local.get $a
    i32.load8_u offset=18446744073709551615)
  (func (export "last_i64") (param $a i64) (result i64)
    local.get $a
    i64.load offset=18446744073709551608)
  (func (export "last_v128") (param $a i64) (result i32)
    i64.const 4096 local.get $a
    v128.load offset=18446744073709551600
    v128.store
    i64.const 4096 i32.load)
  (func (export "past_last_v128") (param $a i64) (result i32)
    i64.const 4096 local.get $a
    v128.load offset=18446744073709551601
    v128.store
    i64.const 4096 i32.load))
