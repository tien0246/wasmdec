(module
  (import "env" "mem" (memory 1))
  (import "env" "table_a" (table $table_a 2 funcref))
  (import "env" "table_b" (table $table_b 2 funcref))
  (func $target)
  (elem (table $table_a) (i32.const 0) func $target)
  (elem (table $table_b) (i32.const 0) func $target)
  (data (i32.const 0) "\00\00\00\00")
)
