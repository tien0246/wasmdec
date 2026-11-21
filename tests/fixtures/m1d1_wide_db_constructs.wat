(module
  (global $sp (mut i32) (i32.const 1024))
  (memory 1)
  (data (i32.const 0) "hi")
  (func (export "f_block") (result i32)
    block
      i32.const 5
      drop
    end
    i32.const 7)
  (func (export "f_local") (result i32)
    (local $t i32)
    i32.const 3
    local.set $t
    local.get $t)
  (func (export "f_global") (result i32)
    global.get $sp)
  (func (export "f_param") (param i32) (result i32)
    local.get 0)
  (func (export "f_const") (result i32)
    i32.const 9)
)
