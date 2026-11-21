(module
  (table $table_a 1 funcref)
  (table $table_b 2 externref)
  (export "table_a" (table $table_a))
  (export "table_b" (table $table_b))
)
