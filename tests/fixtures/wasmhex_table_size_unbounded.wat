(module
  (table $unbounded 2 funcref)
  (func (export "table_size_unbounded") (result i32)
    table.size $unbounded))
