(module
  (memory 1)
  (func $unreachable_grow
    unreachable
    i32.const 1
    memory.grow
    drop)
  (func (export "copy") (param i32 i32)
    local.get 0
    local.get 1
    v128.load
    v128.store))
