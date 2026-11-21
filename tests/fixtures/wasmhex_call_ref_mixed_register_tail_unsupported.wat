(module
  (type $mixed (func (param i32) (result i32 i64)))
  (func $target0 (type $mixed)
    local.get 0
    local.get 0
    i64.extend_i32_s)
  (func $target1 (type $mixed)
    local.get 0
    local.get 0
    i64.extend_i32_u)
  (elem declare func $target0 $target1)
  (func (export "unsupported_tail") (param $value i32) (param $choose i32)
    (result i32 i64)
    (local $target (ref null $mixed))
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
    return_call_ref $mixed))
