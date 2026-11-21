(module
  (type $i32_fn (func (param i32) (result i32)))
  (type $i64_fn (func (param i32) (result i64)))
  (import "env" "identity" (func $identity (type $i32_fn)))
  (table $targets 4 funcref)
  (table $empty 0 funcref)

  (func $wrong_type (type $i64_fn) (param i32) (result i64)
    local.get 0
    i64.extend_i32_s)
  (func $double (type $i32_fn) (param i32) (result i32)
    local.get 0
    i32.const 2
    i32.mul)

  (export "double" (func $double))

  (elem (i32.const 0) $identity $wrong_type)
  (elem (i32.const 3) $double)

  (func (export "tail_dispatch") (param $index i32) (param $value i32)
    (result i32)
    local.get $value
    local.get $index
    return_call_indirect (type $i32_fn))
  (func (export "tail_empty") (param $index i32) (param $value i32)
    (result i32)
    local.get $value
    local.get $index
    return_call_indirect $empty (type $i32_fn))
)
