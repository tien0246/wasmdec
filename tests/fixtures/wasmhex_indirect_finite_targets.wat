(module
  (type $unary_i32 (func (param i32) (result i32)))
  (type $unary_i64 (func (param i64) (result i64)))
  (table $targets 8 funcref)
  (table $targets64 i64 4 funcref)
  (table $explicit_null 1 funcref)
  (func $target0 (export "finite_target0") (type $unary_i32)
    (param i32) (result i32)
    local.get 0 i32.const 10 i32.add)
  (func $target1 (export "finite_target1") (type $unary_i32)
    (param i32) (result i32)
    local.get 0 i32.const 20 i32.add)
  (func $target2 (export "finite_target2") (type $unary_i32)
    (param i32) (result i32)
    local.get 0 i32.const 30 i32.add)
  (func $target3 (export "finite_target3") (type $unary_i32)
    (param i32) (result i32)
    local.get 0 i32.const 40 i32.add)
  (func $wide_target (type $unary_i64)
    (param i64) (result i64)
    local.get 0)
  (func $wide_target2 (type $unary_i64)
    (param i64) (result i64)
    local.get 0)
  (elem (table $targets) (i32.const 0)
    func $target0 $target1 $target2 $target3 $target0 $wide_target $wide_target2)
  (elem (table $targets64) (i64.const 0)
    func $target0 $target1 $target2 $target3)
  (elem (table $explicit_null) (i32.const 0) funcref (ref.null func))

  (func (export "finite_phi") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i32)
      i32.const 0
    else
      i32.const 1
    end
    call_indirect $targets (type $unary_i32))
  (func (export "finite_select") (param i32 i32) (result i32)
    local.get 0
    i32.const 1
    i32.const 3
    local.get 1
    select
    call_indirect $targets (type $unary_i32))
  (func (export "finite_known_unknown") (param i32 i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i32)
      i32.const 2
    else
      local.get 2
    end
    call_indirect $targets (type $unary_i32))
  (func (export "finite_same_target") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i32)
      i32.const 0
    else
      i32.const 4
    end
    call_indirect $targets (type $unary_i32))
  (func (export "finite_oob_arm") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i32)
      i32.const 0
    else
      i32.const 8
    end
    call_indirect $targets (type $unary_i32))
  (func (export "finite_bad_type_arm") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i32)
      i32.const 0
    else
      i32.const 5
    end
    call_indirect $targets (type $unary_i32))
  (func (export "finite_select_constant_condition") (param i32) (result i32)
    local.get 0
    i32.const 1
    i32.const 3
    i32.const 0
    select
    call_indirect $targets (type $unary_i32))
  (func (export "finite_i64_phi_add") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i64)
      i64.const 0
    else
      i64.const 1
    end
    i64.const 1
    i64.add
    call_indirect $targets64 (type $unary_i32))
  (func (export "all_oob_trap") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i32)
      i32.const 8
    else
      i32.const 9
    end
    call_indirect $targets (type $unary_i32))
  (func (export "all_null_trap") (param i32) (result i32)
    local.get 0
    i32.const 7
    call_indirect $targets (type $unary_i32))
  (func (export "all_type_trap") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    if (result i32)
      i32.const 5
    else
      i32.const 6
    end
    call_indirect $targets (type $unary_i32))
  (func (export "all_null_tail_trap") (param i32) (result i32)
    local.get 0
    i32.const 7
    return_call_indirect $targets (type $unary_i32))
  (func (export "explicit_null_trap") (param i32) (result i32)
    local.get 0
    i32.const 0
    call_indirect $explicit_null (type $unary_i32)))
