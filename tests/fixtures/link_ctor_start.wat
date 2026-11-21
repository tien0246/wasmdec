(module
  (import "env" "order" (global $order (mut i32)))
  (func $init
    global.get $order
    i32.const 1
    i32.add
    global.set $order)
  (start $init)
)
