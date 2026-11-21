(module
  (type $unary_i32 (func (param i32) (result i32)))
  (func $known (type $unary_i32) local.get 0)
  (elem declare func $known)
  (func (export "dispatch_unknown_join")
      (param $value i32) (param $choose i32) (param $runtime_target (ref null $unary_i32))
      (result i32)
    (local $target (ref null $unary_i32))
    local.get $choose
    if
      ref.func $known
      local.set $target
    else
      local.get $runtime_target
      local.set $target
    end
    local.get $value
    local.get $target
    call_ref $unary_i32))
