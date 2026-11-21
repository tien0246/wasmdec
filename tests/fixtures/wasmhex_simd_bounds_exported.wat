(module
  (memory (export "memory") 1)
  (func (export "copy") (param i32 i32)
    local.get 0
    local.get 1
    v128.load
    v128.store))
