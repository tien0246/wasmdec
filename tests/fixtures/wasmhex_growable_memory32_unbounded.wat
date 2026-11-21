(module
  (memory 1)
  (func (export "grow") (result i32) i32.const 1 memory.grow)
  (func (export "touch") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.store
    local.get 0 i32.load))
