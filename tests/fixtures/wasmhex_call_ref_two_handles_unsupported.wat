(module
  (type $tuple
    (func (param i32) (result i32 i32 i32 i32 funcref externref)))
  (import "env" "handles" (func $handles (type $tuple)))
  (elem declare func $handles)
  (func (export "two_handles_tail") (param i32)
    (result i32 i32 i32 i32 funcref externref)
    local.get 0
    ref.func $handles
    return_call_ref $tuple))
