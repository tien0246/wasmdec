(module
  (type $pair_sig (func (param i32) (result i32 i32)))
  (type $six_sig (func (param i32) (result i32 i64 i32 i32 i32)))
  (type $scalar_sig (func (param i32) (result i32)))
  (type $effect_sig (func (param i32) (result i32)))
  (import "env" "pair" (func $pair (type $pair_sig)))
  (import "env" "six" (func $six (type $six_sig)))
  (import "env" "effect" (func $effect (type $effect_sig)))
  (table 3 funcref)

  (func $target_scalar (type $scalar_sig) (param i32) (result i32)
    local.get 0
    i32.const 7
    i32.add)

  (elem (i32.const 0) func $pair $six $target_scalar)

  (func (export "direct_import_pair") (param i32) (result i32 i32)
    local.get 0
    call $pair)

  (func (export "direct_import_six") (param i32)
    (result i32 i64 i32 i32 i32)
    local.get 0
    call $six)

  (func (export "indirect_scalar") (param $value i32) (param $index i32)
    (result i32)
    local.get $value
    local.get $index
    call_indirect (type $scalar_sig))

  (func (export "ordered_shift") (param i32 i32) (result i32)
    local.get 0
    call $effect
    local.get 1
    call $effect
    i32.shl))
