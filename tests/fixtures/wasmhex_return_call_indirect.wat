(module
  (type $i32_fn (func (param i32) (result i32)))
  (type $i64_fn (func (param i32) (result i64)))
  (type $wide_fn (func (param i64) (result i64)))
  (type $void_fn (func (param i32)))
  (table $dispatch_table 5 funcref)

  (func $increment (type $i32_fn) (param i32) (result i32)
    local.get 0
    i32.const 1
    i32.add)
  (func $wrong_type (type $i64_fn) (param i32) (result i64)
    local.get 0
    i64.extend_i32_s)
  (func $increment_i64 (type $wide_fn) (param i64) (result i64)
    local.get 0
    i64.const 1
    i64.add)
  (func $discard (type $void_fn) (param i32)
    local.get 0
    drop)

  (elem (i32.const 0) $increment $wrong_type)
  (elem (i32.const 3) $increment_i64 $discard)

  (func (export "tail_valid") (param i32) (result i32)
    local.get 0
    i32.const 0
    return_call_indirect (type $i32_fn))
  (func (export "tail_type_trap") (param i32) (result i32)
    local.get 0
    i32.const 1
    return_call_indirect (type $i32_fn))
  (func (export "tail_null_trap") (param i32) (result i32)
    local.get 0
    i32.const 2
    return_call_indirect (type $i32_fn))
  (func (export "tail_bounds_trap") (param i32) (result i32)
    local.get 0
    i32.const 5
    return_call_indirect (type $i32_fn))
  (func (export "tail_i64") (param i64) (result i64)
    local.get 0
    i32.const 3
    return_call_indirect (type $wide_fn))
  (func (export "tail_void") (param i32)
    local.get 0
    i32.const 4
    return_call_indirect (type $void_fn))
)
