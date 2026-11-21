(module
  (type $unary_i32 (func (param i32) (result i32)))
  (import "env" "targets" (table $imported 2 funcref))
  (table $exported 2 funcref)
  (table $growing 2 3 funcref)
  (table $private 2 funcref)
  (elem declare func $target)
  (func $target (type $unary_i32) (param i32) (result i32)
    local.get 0)
  (func $set_imported (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target
    table.set $imported
    local.get 0
    i32.const 0
    call_indirect $imported (type $unary_i32))
  (func $set_exported (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target
    table.set $exported
    local.get 0
    i32.const 0
    call_indirect $exported (type $unary_i32))
  (func $set_growing (type $unary_i32) (param i32) (result i32)
    ref.func $target
    i32.const 1
    table.grow $growing
    drop
    i32.const 0
    ref.func $target
    table.set $growing
    local.get 0
    i32.const 0
    call_indirect $growing (type $unary_i32))
  (func $set_oob_then_call (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target
    table.set $private
    i32.const 2
    ref.func $target
    table.set $private
    local.get 0
    i32.const 0
    call_indirect $private (type $unary_i32))
  (export "table" (table $exported)))
