(module
  (memory 1)

  (func (export "load_i32") (param i32) (result i32)
    local.get 0
    i32.load offset=4660 align=4)

  (func (export "store_i32") (param i32 i32)
    local.get 0
    local.get 1
    i32.store offset=22136 align=4))
