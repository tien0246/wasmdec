(module
  (type $unary_i32 (func (param i32) (result i32)))
  (import "env" "unknown_target" (global funcref))
  (table $targets 1 funcref)
  (elem (table $targets) (i32.const 0) funcref (global.get 0))
  (func (export "unknown_element_call") (param i32) (result i32)
    local.get 0
    i32.const 0
    call_indirect $targets (type $unary_i32)))
