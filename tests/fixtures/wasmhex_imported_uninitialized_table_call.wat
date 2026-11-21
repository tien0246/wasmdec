(module
  (type $unary (func (param i32) (result i32)))
  (import "env" "table" (table 1 funcref))
  (func (export "indirect") (param $value i32) (result i32)
    local.get $value
    i32.const 0
    call_indirect (type $unary)))
