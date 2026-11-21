(module
  (global $g32 (mut i32) (i32.const 1024))
  (func (export "get_g32") (result i32)
    global.get $g32)
)
