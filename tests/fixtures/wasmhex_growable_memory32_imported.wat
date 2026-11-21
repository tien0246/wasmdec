(module
  (import "env" "memory" (memory 1 3))
  (func (export "grow") (result i32) i32.const 1 memory.grow)
  (func (export "size") (result i32) memory.size)
  (func (export "touch") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.store offset=4
    local.get 0 i32.load offset=4)
  (func (export "load32_offset4") (param i32 i32) (result i32)
    local.get 1 i32.load offset=4))
