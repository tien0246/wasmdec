(module
  (memory 1)
  (func (export "bad_lane") (param i32) (result i32)
    local.get 0
    i32.const 0
    v128.load
    v128.load8_lane 16
    drop
    i32.const 42))
