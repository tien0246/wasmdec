(module
  (memory 1 2)
  (func (export "grow") (result i32)
    i32.const 1
    memory.grow)
  (func (export "write_last")
    i32.const 65543
    i32.const 127
    i32.store8)
  (func (export "load_crossing") (param $address i32) (result i32)
    local.get $address
    v128.load
    drop
    i32.const 0)
  (func (export "copy_crossing") (param $address i32) (result i32)
    i32.const 65544
    local.get $address
    v128.load
    v128.store
    i32.const 65559
    i32.load8_u))
