(module
  (memory (export "memory") 1)
  (func (export "memory_atomic_notify") (param i32 i32) (result i32)
    local.get 0 local.get 1 memory.atomic.notify offset=64 align=4)
  (func (export "memory_atomic_wait32") (param i32 i32 i64) (result i32)
    local.get 0 local.get 1 local.get 2 memory.atomic.wait32 offset=64 align=4)
  (func (export "memory_atomic_wait64") (param i32 i64 i64) (result i32)
    local.get 0 local.get 1 local.get 2 memory.atomic.wait64 offset=64 align=8)
)
