(module
  (import "env" "bar" (func $bar (result i32)))
  (func (export "call_bar") (result i32) (call $bar))
)
