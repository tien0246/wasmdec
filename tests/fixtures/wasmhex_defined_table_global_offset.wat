(module
  (type $unary (func (param i32) (result i32)))
  (import "env" "offset" (global $offset i32))
  (table $targets 4 4 funcref)
  (func $target (type $unary)
    local.get 0
    i32.const 7
    i32.add)
  (elem (table $targets) (global.get $offset) func $target)
  (func (export "direct") (result i32)
    i32.const 42)
  (func (export "indirect") (param $value i32) (result i32)
    local.get $value
    i32.const 2
    call_indirect $targets (type $unary)))
