(module
  (type $i32 (func (param i32) (result i32)))
  (func $target (type $i32)
    local.get 0
    i32.const 1
    i32.add)
  (export "target" (func $target))
  (func (export "call_ref_same_join") (param i32 i32) (result i32)
       (local (ref null $i32))
    local.get 1
    if
      ref.func $target
      local.set 2
    else
      ref.func $target
      local.set 2
    end
    local.get 0
    local.get 2
    call_ref $i32)
  (func (export "call_ref_same_no_else") (param i32 i32) (result i32)
       (local (ref null $i32))
    ref.func $target
    local.set 2
    local.get 1
    if
      ref.func $target
      local.set 2
    end
    local.get 0
    local.get 2
    call_ref $i32))
