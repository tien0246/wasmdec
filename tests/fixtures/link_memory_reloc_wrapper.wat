(module
  (import "env" "a" (memory 1))
  (import "env" "b" (memory 1))
  (import "env" "read_value" (func $read_value (result i32)))
  (func (export "read_value_wrapped") (result i32) (call $read_value)))
