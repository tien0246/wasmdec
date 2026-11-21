(module
  (type $unary_i32 (func (param i32) (result i32)))
  (type $unary_i64 (func (param i64) (result i64)))
  (func $target0 (type $unary_i32) local.get 0)
  (func $target1 (type $unary_i32)
    local.get 0
    i32.const 1
    i32.add)
  (elem declare func $target0 $target1)
  (func (export "dispatch_encoded_mismatch")
      (param $value i64) (param $choose i32) (result i64)
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
    call_ref $unary_i64))
