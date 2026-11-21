(module
  (table $table 1 funcref)
  (func $target)
  (elem $pass func $target)
  (func (export "table_bulk_ops")
    i32.const 0 i32.const 0 i32.const 1 table.init $table $pass
    elem.drop $pass
    i32.const 0 i32.const 0 i32.const 1 table.copy $table $table))
