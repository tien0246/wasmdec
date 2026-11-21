(module
  (memory i64 1 4)

  (func (export "memory_pages64") (result i64)
    memory.size)
  (func (export "grow_memory64") (param $pages i64) (result i64)
    local.get $pages
    memory.grow))
