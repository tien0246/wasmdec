(module
  (import "env" "call" (func $call (result i32)))
  (func $run (export "run") (result i32) (call $call)))
