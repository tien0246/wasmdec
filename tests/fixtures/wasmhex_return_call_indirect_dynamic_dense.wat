(module
  (type $unary_i32 (func (param i32) (result i32)))
  (table $dispatch_table 2 funcref)

  (func $add_one (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 1
    i32.add)
  (func $double (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 2
    i32.mul)
  (elem (i32.const 0) $add_one $double)

  (func (export "tail_dispatch") (param $index i32) (param $value i32) (result i32)
    local.get $value
    local.get $index
    return_call_indirect (type $unary_i32))
)
