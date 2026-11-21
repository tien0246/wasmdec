(module
  (type $i32_fn (func (param i32) (result i32)))
  (type $i64_fn (func (param i32) (result i64)))
  (import "env" "identity" (func $identity (type $i32_fn)))
  (table $targets 8192 funcref)

  (func $wrong_type (type $i64_fn) (param i32) (result i64)
    local.get 0
    i64.extend_i32_s)
  (func $last (type $i32_fn) (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (export "last" (func $last))

  (elem (i32.const 0) $identity)
  (elem (i32.const 4096) $wrong_type)
  (elem (i32.const 8191) $last)

  (func (export "dispatch") (param $index i32) (param $value i32)
    (result i32)
    local.get $value
    local.get $index
    call_indirect $targets (type $i32_fn))

  (func (export "tail_dispatch") (param $index i32) (param $value i32)
    (result i32)
    local.get $value
    local.get $index
    return_call_indirect $targets (type $i32_fn))
)
