(module
  (type $unary_i32 (func (param i32) (result i32)))
  (type $unary_i64 (func (param i64) (result i64)))
  (table $mixed 1 funcref)
  (table $nulls 1 funcref)
  (table $wrong_types 1 funcref)
  (table $bounds 1 funcref)
  (elem declare func $target $wrong_type)
  (func $target (type $unary_i32) (param i32) (result i32)
    local.get 0)
  (func $wrong_type (type $unary_i64) (param i64) (result i64)
    local.get 0)
  (func $mixed_join (param i32 i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.func $target
      table.set $mixed
    else
      local.get 1
      if
        i32.const 0
        ref.null func
        table.set $mixed
      else
        i32.const 0
        ref.func $wrong_type
        table.set $mixed
      end
    end
    local.get 2
    i32.const 0
    call_indirect $mixed (type $unary_i32))
  (func $null_join (param i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.null func
      table.set $nulls
    else
      i32.const 0
      ref.null func
      table.set $nulls
    end
    local.get 1
    i32.const 0
    call_indirect $nulls (type $unary_i32))
  (func $wrong_type_join (param i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.func $wrong_type
      table.set $wrong_types
    else
      i32.const 0
      ref.func $wrong_type
      table.set $wrong_types
    end
    local.get 1
    i32.const 0
    call_indirect $wrong_types (type $unary_i32))
  (func $bounds_after_join (param i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.func $target
      table.set $bounds
    else
      i32.const 0
      ref.null func
      table.set $bounds
    end
    local.get 1
    i32.const 1
    call_indirect $bounds (type $unary_i32))
  (export "target" (func $target))
  (export "wrong_type" (func $wrong_type))
  (export "mixed_join" (func $mixed_join))
  (export "null_join" (func $null_join))
  (export "wrong_type_join" (func $wrong_type_join))
  (export "bounds_after_join" (func $bounds_after_join)))
