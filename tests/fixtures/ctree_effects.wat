(module
  (import "env" "effect" (func $effect (param i32) (result i32)))
  (func (export "shift_effects") (param i32 i32) (result i32)
    (i32.shl (call $effect (local.get 0)) (call $effect (local.get 1))))
  (func (export "bitcount_effects") (param i32) (result i32)
    (i32.popcnt (call $effect (local.get 0))))
  (func (export "pure_shift") (param i32 i32) (result i32)
    (i32.shl (local.get 0) (local.get 1)))
)
