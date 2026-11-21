(module
  (memory 0)
  (func (export "load") (param i32) (result i32)
    local.get 0
    v128.load
    drop
    i32.const 42))
