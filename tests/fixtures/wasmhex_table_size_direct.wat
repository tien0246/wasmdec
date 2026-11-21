(module
  (table $fixed32 7 7 funcref)
  (table $fixed64 i64 9 9 funcref)

  (func (export "table_size_fixed32") (result i32)
    table.size $fixed32)
  (func (export "table_size_fixed64") (result i64)
    table.size $fixed64))
