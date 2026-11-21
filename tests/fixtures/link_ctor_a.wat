(module
  (import "env" "order" (global $order (mut i32)))
  (func $init_a
    global.get $order
    i32.const 10
    i32.mul
    i32.const 1
    i32.add
    global.set $order)
)
