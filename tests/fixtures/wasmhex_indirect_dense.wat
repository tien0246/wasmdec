(module
  (type $binary (func (param i32 i32) (result i32)))
  (table 2 funcref)
  (elem (i32.const 0) $add $sub)

  (func $add (type $binary) (param $left i32) (param $right i32) (result i32)
    local.get $left
    local.get $right
    i32.add)
  (func $sub (type $binary) (param $left i32) (param $right i32) (result i32)
    local.get $left
    local.get $right
    i32.sub)

  (func (export "dispatch") (param $index i32) (param $left i32) (param $right i32) (result i32)
    local.get $left
    local.get $right
    local.get $index
    call_indirect (type $binary)))
