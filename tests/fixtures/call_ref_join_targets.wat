(module
  (type $t (func (param i32) (result i32)))
  (func $target0 (type $t) local.get 0)
  (func $target1 (type $t) (param i32) (result i32)
    local.get 0
    i32.const 1
    i32.add)
  (elem declare func $target0 $target1)
  (func (export "call_phi") (param i32 i32) (result i32)
       (local (ref null $t))
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
    call_ref $t)
  (func (export "tail_phi") (param i32 i32) (result i32)
       (local (ref null $t))
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
    return_call_ref $t)
  (func (export "unknown_ref") (param i32 (ref null $t)) (result i32)
    local.get 0
    local.get 1
    call_ref $t)
  (func (export "unknown_phi") (param i32 (ref null $t) i32) (result i32)
       (local (ref null $t))
    local.get 2
    if
      ref.func $target0
      local.set 3
    else
      local.get 1
      local.set 3
    end
    local.get 0
    local.get 3
    call_ref $t))
