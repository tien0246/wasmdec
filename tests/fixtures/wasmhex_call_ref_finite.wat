(module
  (type $unary_i32 (func (param i32) (result i32)))
  (func $target0 (export "target0") (type $unary_i32)
    local.get 0)
  (func $target1 (export "target1") (type $unary_i32)
    local.get 0
    i32.const 7
    i32.add)
  (elem declare func $target0 $target1)

  (func (export "dispatch") (param $value i32) (param $choose i32) (result i32)
    (local $target (ref null $unary_i32))
    local.get $choose
    if
      ref.func $target0
      local.set $target
    else
      ref.func $target1
      local.set $target
    end
    local.get $value
    local.get $target
    call_ref $unary_i32)

  (func (export "dispatch_tail") (param $value i32) (param $choose i32) (result i32)
    (local $target (ref null $unary_i32))
    local.get $choose
    if
      ref.func $target0
      local.set $target
    else
      ref.func $target1
      local.set $target
    end
    local.get $value
    local.get $target
    return_call_ref $unary_i32))
