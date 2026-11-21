(module
  (type $pair_i64 (func (param i32) (result i32 i64)))
  (type $pair (func (param i32) (result i32 i32)))
  (type $six (func (param i32) (result i32 i32 i32 i32 i32 i32)))

  (func $pair_i64_target (export "pair_i64_target") (type $pair_i64)
    local.get 0
    local.get 0
    i64.extend_i32_s)
  (func $pair_target0 (export "pair_target0") (type $pair)
    local.get 0
    local.get 0
    i32.const 1
    i32.add)
  (func $pair_target1 (export "pair_target1") (type $pair)
    local.get 0
    i32.const 2
    i32.mul
    local.get 0
    i32.const 3
    i32.mul)
  (func $six_target0 (export "six_target0") (type $six)
    local.get 0
    local.get 0 i32.const 1 i32.add
    local.get 0 i32.const 2 i32.add
    local.get 0 i32.const 3 i32.add
    local.get 0 i32.const 4 i32.add
    local.get 0 i32.const 5 i32.add)
  (func $six_target1 (export "six_target1") (type $six)
    local.get 0 i32.const 2 i32.mul
    local.get 0 i32.const 2 i32.mul i32.const 1 i32.add
    local.get 0 i32.const 2 i32.mul i32.const 2 i32.add
    local.get 0 i32.const 2 i32.mul i32.const 3 i32.add
    local.get 0 i32.const 2 i32.mul i32.const 4 i32.add
    local.get 0 i32.const 2 i32.mul i32.const 5 i32.add)
  (elem declare func $pair_i64_target $pair_target0 $pair_target1
                       $six_target0 $six_target1)

  (func (export "call_ref_pair_i64") (param $value i32) (result i32 i64)
    local.get $value
    ref.func $pair_i64_target
    call_ref $pair_i64)

  (func (export "call_ref_pair_join") (param $value i32) (param $choose i32)
    (result i32 i32)
    (local $target (ref null $pair))
    local.get $choose
    if
      ref.func $pair_target0
      local.set $target
    else
      ref.func $pair_target1
      local.set $target
    end
    local.get $value
    local.get $target
    call_ref $pair)

  (func (export "return_call_ref_pair_join")
    (param $value i32) (param $choose i32) (result i32 i32)
    (local $target (ref null $pair))
    local.get $choose
    if
      ref.func $pair_target0
      local.set $target
    else
      ref.func $pair_target1
      local.set $target
    end
    local.get $value
    local.get $target
    return_call_ref $pair)

  (func (export "call_ref_six_join") (param $value i32) (param $choose i32)
    (result i32 i32 i32 i32 i32 i32)
    (local $target (ref null $six))
    local.get $choose
    if
      ref.func $six_target0
      local.set $target
    else
      ref.func $six_target1
      local.set $target
    end
    local.get $value
    local.get $target
    call_ref $six)

  (func (export "return_call_ref_six_join")
    (param $value i32) (param $choose i32)
    (result i32 i32 i32 i32 i32 i32)
    (local $target (ref null $six))
    local.get $choose
    if
      ref.func $six_target0
      local.set $target
    else
      ref.func $six_target1
      local.set $target
    end
    local.get $value
    local.get $target
    return_call_ref $six))
