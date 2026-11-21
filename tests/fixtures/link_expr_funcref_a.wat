(module
  (import "env" "t" (table 2 funcref))
  (func $a (result i32) (i32.const 11))
  (elem (table 0) (i32.const 0) funcref (ref.func $a))
)
