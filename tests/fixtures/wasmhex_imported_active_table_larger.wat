(module
  (type $unary (func (param i32) (result i32)))
  (import "env" "table" (table 1 4 funcref))

  (func $table_entry (type $unary)
    local.get 0
    i32.const 77
    i32.add)
  (elem (table 0) (i32.const 2) func $table_entry)

  (func (export "direct") (result i32)
    i32.const 42))
