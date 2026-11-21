(module
  (type $sig (func (param i32) (result i32)))
  (table 1 funcref)
  (func $target (type $sig) (param i32) (result i32)
    local.get 0)
  (elem (i32.const 0) $target)
  (func $caller (type $sig) (param i32) (result i32)
    local.get 0
    i32.const 0
    call_indirect (type $sig))
  (func $dead_write
    unreachable
    i32.const 0
    ref.null func
    table.set 0))
