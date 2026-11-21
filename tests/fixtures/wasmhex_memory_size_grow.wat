(module
  (memory (export "memory") 1 4)

  (func (export "memory_pages") (result i32)
    memory.size)
  (func (export "grow_memory") (param $pages i32) (result i32)
    local.get $pages
    memory.grow))
