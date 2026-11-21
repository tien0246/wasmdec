(module
  (type $good (func (param i32) (result i32)))
  (type $bad (func (param i32) (result i64)))
  (func $target_good (type $good)
    local.get 0
    i32.const 1
    i32.add)
  (func $target_bad (type $bad)
    local.get 0
    i64.extend_i32_s)
  (table 3 funcref)
  (elem (i32.const 0) func $target_good $target_bad)
  (func (export "call_good") (param i32) (result i32)
    local.get 0
    i32.const 0
    call_indirect (type $good))
  (func (export "trap_type") (param i32) (result i32)
    local.get 0
    i32.const 1
    call_indirect (type $good))
  (func (export "trap_null") (param i32) (result i32)
    local.get 0
    i32.const 2
    call_indirect (type $good))
  (func (export "trap_bounds") (param i32) (result i32)
    local.get 0
    i32.const 3
    call_indirect (type $good)))
