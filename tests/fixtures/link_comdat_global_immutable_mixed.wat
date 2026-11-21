(module
  (global $same i32 (i32.const 22))
  (global $helper i32 (i32.const 7))
  (func (export "read_same") (result i32) (global.get $same))
)
