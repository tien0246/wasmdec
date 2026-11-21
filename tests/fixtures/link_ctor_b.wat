(module
  (import "env" "order" (global $order (mut i32)))
  (func $init_b
    global.get $order
    i32.const 10
    i32.mul
    i32.const 2
    i32.add
    global.set $order)
)
