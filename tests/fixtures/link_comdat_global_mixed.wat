(module
  (global $same (mut i32) (i32.const 22))
  (global $helper i32 (i32.const 7))
  (func (export "read_same") (result i32) (global.get $same))
  (func (export "write_same") (param i32) (global.set $same (local.get 0)))
  (func (export "read_helper") (result i32) (global.get $helper))
)
