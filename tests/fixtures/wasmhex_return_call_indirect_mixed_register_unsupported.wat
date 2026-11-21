(module
  (type $pair_fn (func (param i32) (result i32 i64)))
  (table $targets 1 funcref)

  (func $pair_identity (type $pair_fn) (param i32) (result i32 i64)
    local.get 0
    local.get 0
    i64.extend_i32_s)
  (elem (i32.const 0) $pair_identity)

  (func (export "tail_pair_dispatch") (param $index i32) (param $value i32)
    (result i32 i64)
    local.get $value
    local.get $index
    return_call_indirect $targets (type $pair_fn))
)
