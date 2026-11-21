(module
  (type $unary_i32 (func (param i32) (result i32)))
  (table $targets 2 funcref)
  (func $target0 (export "target0") (type $unary_i32)
    (param i32) (result i32)
    local.get 0)
  (func $target1 (export "target1") (type $unary_i32)
    (param i32) (result i32)
    local.get 0)
  (elem (table $targets) (i32.const 0) func $target0 $target1)
  (func $mutate_table (param i32 funcref)
    local.get 0
    local.get 1
    table.set $targets)
  (func (export "mutable_table_index") (param i32) (result i32)
    local.get 0
    i32.const 1
    call_indirect $targets (type $unary_i32)))
