(module
  (type $unary_i32 (func (param i32) (result i32)))
  (type $unary_i64 (func (param i64) (result i64)))
  (table $targets 3 3 funcref)
  (table $active_targets 3 3 funcref)
  (table $active_null 3 3 funcref)
  (table $active_mismatch 3 3 funcref)
  (table $active_clobbered 3 3 funcref)
  (elem declare func $target)
  (elem (table $active_targets) (i32.const 0) func $target)
  (elem (table $active_null) (i32.const 0) funcref (ref.null func))
  (elem (table $active_mismatch) (i32.const 0) func $target_i64)
  (elem (table $active_clobbered) (i32.const 0) func $target)
  (func $target (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (func $target_i64 (type $unary_i64) (param i64) (result i64)
    local.get 0)
  (func $noop)
  (func $copy_then_call (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target
    table.set $targets
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $targets $targets
    local.get 0
    i32.const 1
    call_indirect $targets (type $unary_i32))
  (func $copy_active_then_call (type $unary_i32) (param i32) (result i32)
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $active_targets $active_targets
    local.get 0
    i32.const 1
    call_indirect $active_targets (type $unary_i32))
  (func $copy_active_null (type $unary_i32) (param i32) (result i32)
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $active_null $active_null
    local.get 0
    i32.const 1
    call_indirect $active_null (type $unary_i32))
  (func $copy_active_type_mismatch (type $unary_i32) (param i32) (result i32)
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $active_mismatch $active_mismatch
    local.get 0
    i32.const 1
    call_indirect $active_mismatch (type $unary_i32))
  (func $copy_active_clobbered (type $unary_i32) (param i32) (result i32)
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $active_clobbered $active_clobbered
    call $noop
    local.get 0
    i32.const 1
    call_indirect $active_clobbered (type $unary_i32))
  (export "copy_then_call" (func $copy_then_call))
  (export "copy_active_then_call" (func $copy_active_then_call))
  (export "copy_active_null" (func $copy_active_null))
  (export "copy_active_type_mismatch" (func $copy_active_type_mismatch))
  (export "copy_active_clobbered" (func $copy_active_clobbered)))
