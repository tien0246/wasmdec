(module
  (memory i64 1 2)
  (memory i64 1 2)
  (func (export "grow") (result i64) i64.const 1 memory.grow)
  (func (export "touch") (param i64 i32) (result i32)
    local.get 0 local.get 1 i32.store offset=4
    local.get 0 i32.load offset=4))
