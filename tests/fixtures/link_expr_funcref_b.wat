(module
  (import "env" "t" (table 2 funcref))
  (func $b (result i32) (i32.const 22))
  (elem (table 0) (i32.const 1) funcref (ref.func $b))
)
