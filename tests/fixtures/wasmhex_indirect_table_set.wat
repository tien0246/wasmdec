(module
  (type $unary_i32 (func (param i32) (result i32)))
  (table $targets 2 funcref)
  (elem declare func $target0 $target1)
  (func $target0 (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 10
    i32.add)
  (func $target1 (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (func $noop)
  (func $set_then_call (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $targets
    local.get 0
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (func $alias_then_call (type $unary_i32) (param i32) (result i32)
    (local funcref)
    ref.func $target1
    local.set 1
    i32.const 0
    local.get 1
    table.set $targets
    local.get 0
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (func $same_target_join (param i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.func $target1
      table.set $targets
    else
      i32.const 0
      ref.func $target1
      table.set $targets
    end
    local.get 1
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (func $different_target_join (param i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.func $target0
      table.set $targets
    else
      i32.const 0
      ref.func $target1
      table.set $targets
    end
    local.get 1
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (func $missing_write_join (param i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.func $target1
      table.set $targets
    end
    local.get 1
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (func $call_between_write_and_indirect (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $targets
    call $noop
    local.get 0
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (func $unknown_reference (param i32 funcref i32) (result i32)
    i32.const 0
    local.get 1
    table.set $targets
    local.get 2
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (func $unknown_write_index (param i32 i32) (result i32)
    local.get 1
    ref.func $target1
    table.set $targets
    local.get 0
    i32.const 0
    call_indirect $targets (type $unary_i32))
  (export "set_then_call" (func $set_then_call))
  (export "same_target_join" (func $same_target_join))
  (export "different_target_join" (func $different_target_join))
  (export "missing_write_join" (func $missing_write_join))
  (export "call_between_write_and_indirect" (func $call_between_write_and_indirect))
  (export "unknown_reference" (func $unknown_reference))
  (export "unknown_write_index" (func $unknown_write_index)))
