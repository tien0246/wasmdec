(module
  (type $unary_i32 (func (param i32) (result i32)))
  (table $targets 4 funcref)
  (func $target0 (export "target0") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 10
    i32.add)
  (func $target1 (export "target1") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (func $target2 (export "target2") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 30
    i32.add)
  (func $target3 (export "target3") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 40
    i32.add)
  (elem (table $targets) (i32.const 0)
    func $target0 $target1 $target2 $target3)
  (func (export "local_alias_chain") (param i32) (result i32) (local i32 i32)
    i32.const 1
    local.set 1
    local.get 1
    local.set 2
    local.get 0
    local.get 2
    call_indirect $targets (type $unary_i32))
  (func (export "join_same_value") (param i32) (result i32) (local i32)
    local.get 0
    if (result i32)
      i32.const 2
    else
      i32.const 2
    end
    local.set 1
    local.get 0
    local.get 1
    call_indirect $targets (type $unary_i32))
  (func (export "join_ambiguous") (param i32) (result i32) (local i32)
    local.get 0
    if (result i32)
      i32.const 0
    else
      i32.const 1
    end
    local.set 1
    local.get 0
    local.get 1
    call_indirect $targets (type $unary_i32))
  (func (export "index_shift_add") (param i32) (result i32)
    local.get 0
    i32.const 1
    i32.const 1
    i32.shl
    i32.const 1
    i32.add
    call_indirect $targets (type $unary_i32))
  (func (export "index_div_u") (param i32) (result i32)
    local.get 0
    i32.const 6
    i32.const 2
    i32.div_u
    call_indirect $targets (type $unary_i32))
  (func (export "index_div_s") (param i32) (result i32)
    local.get 0
    i32.const 6
    i32.const 2
    i32.div_s
    call_indirect $targets (type $unary_i32))
  (func (export "index_ctz") (param i32) (result i32)
    local.get 0
    i32.const 1
    i32.ctz
    call_indirect $targets (type $unary_i32))
  (func (export "index_clz") (param i32) (result i32)
    local.get 0
    i32.const 1073741824
    i32.clz
    call_indirect $targets (type $unary_i32))
  (func (export "index_popcnt") (param i32) (result i32)
    local.get 0
    i32.const 3
    i32.popcnt
    call_indirect $targets (type $unary_i32))
  (func (export "index_div_zero") (param i32) (result i32)
    local.get 0
    i32.const 1
    i32.const 0
    i32.div_u
    call_indirect $targets (type $unary_i32))
  (func (export "dynamic_index") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    call_indirect $targets (type $unary_i32)))
