(module
  (type $i32 (func (param i32) (result i32)))
  (func $target0 (type $i32)
    local.get 0)
  (func $target1 (type $i32)
    local.get 0
    i32.const 1
    i32.add)
  (export "target0" (func $target0))
  (export "target1" (func $target1))
  (func (export "call_ref_join") (param i32 i32) (result i32)
       (local (ref null $i32))
    local.get 1
    if
      ref.func $target0
      local.set 2
    else
      ref.func $target1
      local.set 2
    end
    local.get 0
    local.get 2
    call_ref $i32))
