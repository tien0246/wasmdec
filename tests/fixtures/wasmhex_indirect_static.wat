(module
  (type $unary_i32 (func (param i32) (result i32)))
  (type $no_args (func))
  (table 1 funcref)
  (table $targets 3 funcref)
  (func $add10 (export "add10") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 10
    i32.add)
  (func $add20 (export "add20") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (func $no_op (type $no_args))
  (elem (table $targets) (i32.const 0) func $add10 $add20 $no_op)
  (func (export "static_slot") (param i32) (result i32)
    local.get 0
    i32.const 1
    call_indirect $targets (type $unary_i32))
  (func (export "static_local_alias") (param i32) (result i32) (local i32)
    i32.const 1
    local.set 1
    local.get 0
    local.get 1
    call_indirect $targets (type $unary_i32))
  (func (export "static_constant_add") (param i32) (result i32)
    local.get 0
    i32.const 0
    i32.const 1
    i32.add
    call_indirect $targets (type $unary_i32))
  (func (export "dynamic_slot") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    call_indirect $targets (type $unary_i32))
  (func (export "static_type_mismatch") (param i32) (result i32)
    local.get 0
    i32.const 2
    call_indirect $targets (type $unary_i32)))
