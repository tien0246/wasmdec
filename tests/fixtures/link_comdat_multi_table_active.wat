(module
  (table $table_a 1 funcref)
  (table $table_b 2 externref)
  (func $entry)
  (elem (i32.const 0) $entry)
  (global $keep i32 (i32.const 7))
)
