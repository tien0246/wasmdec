(module
  (type $unary_i32 (func (param i32) (result i32)))
  (table $targets 3 3 funcref)
  (elem declare func $target0 $target1)
  (elem (table $targets) (i32.const 0) func $target0)
  (elem (table $targets) (i32.const 2) func $target1)
  (data "x")
  (func $target0 (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 10
    i32.add)
  (func $target1 (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (func $hidden_writer (type $unary_i32) (param i32) (result i32)
    i32.const 0
    i32.const 2
    i32.const 1
    table.copy $targets $targets
    data.drop 0
    local.get 0
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (export "hidden_writer" (func $hidden_writer))
  (func $copy_after_hidden (type $unary_i32) (param i32) (result i32)
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $targets $targets
    local.get 0
    i32.const 1
    call_indirect $targets (type $unary_i32)))
