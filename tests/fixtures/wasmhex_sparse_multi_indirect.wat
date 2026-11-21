(module
  (type $pair_fn (func (param i32) (result i32 i32)))
  (type $wide_fn (func (param i32) (result i64)))
  (type $mixed_fn (func (param i32) (result i32 f64 i32)))
  (type $six_fn (func (param i32) (result i32 i32 i32 i32 i32 i32)))
  (table $targets 8192 8192 funcref)

  (func $pair_add (type $pair_fn) (param i32) (result i32 i32)
    local.get 0
    i32.const 10
    i32.add
    local.get 0
    i32.const 3
    i32.sub)
  (func $pair_scale (type $pair_fn) (param i32) (result i32 i32)
    local.get 0
    i32.const 2
    i32.mul
    local.get 0
    i32.const 3
    i32.mul)
  (func $wide_bias (type $wide_fn) (param i32) (result i64)
    local.get 0
    i64.extend_i32_u
    i64.const 4294967296
    i64.add)
  (func $mixed_identity (type $mixed_fn) (param i32) (result i32 f64 i32)
    local.get 0
    local.get 0
    f64.convert_i32_s
    local.get 0
    i32.const 100
    i32.add)
  (func $mixed_scale (type $mixed_fn) (param i32) (result i32 f64 i32)
    local.get 0
    i32.const 2
    i32.mul
    local.get 0
    i32.const 3
    i32.mul
    f64.convert_i32_s
    local.get 0
    i32.const 3
    i32.sub)
  (func $six_values (type $six_fn) (param i32) (result i32 i32 i32 i32 i32 i32)
    local.get 0
    local.get 0
    i32.const 1
    i32.add
    local.get 0
    i32.const 2
    i32.add
    local.get 0
    i32.const 3
    i32.add
    local.get 0
    i32.const 4
    i32.add
    local.get 0
    i32.const 5
    i32.add)

  (elem (i32.const 0) $pair_add)
  (elem (i32.const 1) $mixed_identity)
  (elem (i32.const 5000) $wide_bias)
  (elem (i32.const 6000) $six_values)
  (elem (i32.const 8190) $mixed_scale)
  (elem (i32.const 8191) $pair_scale)

  (func (export "dispatch_pair") (param $index i32) (param $value i32)
    (result i32 i32)
    local.get $value
    local.get $index
    call_indirect $targets (type $pair_fn))
  (func (export "dispatch_wide") (param $index i32) (param $value i32)
    (result i64)
    local.get $value
    local.get $index
    call_indirect $targets (type $wide_fn))
  (func (export "dispatch_mixed") (param $index i32) (param $value i32)
    (result i32 f64 i32)
    local.get $value
    local.get $index
    call_indirect $targets (type $mixed_fn))
  (func (export "dispatch_six") (param $index i32) (param $value i32)
    (result i32 i32 i32 i32 i32 i32)
    local.get $value
    local.get $index
    call_indirect $targets (type $six_fn))
  (func (export "tail_pair_dispatch") (param $index i32) (param $value i32)
    (result i32 i32)
    local.get $value
    local.get $index
    return_call_indirect $targets (type $pair_fn))
  (func (export "tail_six_dispatch") (param $index i32) (param $value i32)
    (result i32 i32 i32 i32 i32 i32)
    local.get $value
    local.get $index
    return_call_indirect $targets (type $six_fn)))
