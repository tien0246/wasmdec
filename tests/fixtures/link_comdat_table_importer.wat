(module
  (import "env" "same" (table 2 funcref))
  (func (export "imported_size") (result i32) (table.size 0))
)
