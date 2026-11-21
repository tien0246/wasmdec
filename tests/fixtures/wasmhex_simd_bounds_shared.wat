(module
  (memory 1 2 shared)
  (func (export "copy") (param i32 i32)
    local.get 0
    local.get 1
    v128.load
    v128.store))
