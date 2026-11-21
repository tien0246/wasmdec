(module
  (type $unary (func (param i32) (result i32)))
  (table $exported (export "exported_table") 1 1 funcref)
  (table $table64 i64 1 1 funcref)
  (func $target (type $unary)
    local.get 0
    i32.const 7
    i32.add)
  (elem (table $exported) (i32.const 0) func $target)
  (elem (table $table64) (i64.const 0) func $target)
  (func $call_exported (export "call_exported") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    call_indirect $exported (type $unary))
  (func $call_table64 (export "call_table64") (param i32 i64) (result i32)
    local.get 0
    local.get 1
    call_indirect $table64 (type $unary))
)
