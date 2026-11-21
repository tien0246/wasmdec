(module
  (type $unary (func (param i32) (result i32)))
  (import "env" "table" (table 0 funcref))
  (func (export "indirect") (param $value i32) (param $index i32) (result i32)
    local.get $value
    local.get $index
    call_indirect (type $unary)))
