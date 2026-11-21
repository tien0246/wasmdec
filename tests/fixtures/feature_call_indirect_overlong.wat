(module
  (type $unary_i32 (func (param i32) (result i32)))
  (table $targets 1 funcref)
  (func (export "invoke") (param i32) (result i32)
    local.get 0
    i32.const 0
    call_indirect $targets (type $unary_i32)))
