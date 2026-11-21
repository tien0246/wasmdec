(module
  (table $growable 2 5 funcref)
  (func (export "table_size_growable") (result i32)
    table.size $growable))
