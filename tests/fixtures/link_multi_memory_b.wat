(module
  (import "env" "b" (memory $b 1))
  (memory $d 1)
  (data (memory $d) (i32.const 0) "\44\00\00\00")
  (func $read_b_copy (export "read_b2") (result i32) (i32.load (i32.const 0)))
  (func $read_d (export "read_d") (result i32) (i32.load (memory $d) (i32.const 0)))
  (export "memory_b2" (memory $b))
  (export "memory_d" (memory $d)))
