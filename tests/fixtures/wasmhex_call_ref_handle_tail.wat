(module
  (type $ref_pair (func (param i32) (result i32 funcref)))
  (type $extern_pair (func (param i32) (result i32 externref)))
  (type $wide_ref_pair (func (param i32)
    (result i32 i32 i32 i32 funcref)))
  (type $i32_fn (func (param i32) (result i32)))
  (func $handler0 (export "handler0") (type $i32_fn) local.get 0)
  (func $handler1 (export "handler1") (type $i32_fn) local.get 0 i32.const 1 i32.add)
  (func $ref_target0 (export "ref_target0") (type $ref_pair) local.get 0 ref.func $handler0)
  (func $ref_target1 (export "ref_target1") (type $ref_pair)
    local.get 0 i32.const 1 i32.add
    ref.func $handler1)
  (func $extern_target0 (export "extern_target0") (type $extern_pair) local.get 0 ref.null extern)
  (func $extern_target1 (export "extern_target1") (type $extern_pair)
    local.get 0 i32.const 1 i32.add
    ref.null extern)
  (func $wide_ref_target0 (export "wide_ref_target0") (type $wide_ref_pair)
    local.get 0
    local.get 0 i32.const 1 i32.add
    local.get 0 i32.const 2 i32.add
    local.get 0 i32.const 3 i32.add
    ref.func $handler0)
  (func $wide_ref_target1 (export "wide_ref_target1") (type $wide_ref_pair)
    local.get 0 i32.const 1 i32.add
    local.get 0 i32.const 2 i32.add
    local.get 0 i32.const 3 i32.add
    local.get 0 i32.const 4 i32.add
    ref.func $handler1)
  (elem declare func $handler0 $handler1 $ref_target0 $ref_target1
                       $extern_target0 $extern_target1
                       $wide_ref_target0 $wide_ref_target1)

  (func (export "tail_funcref_join")
    (param $value i32) (param $choose i32) (result i32 funcref)
    (local $target (ref null $ref_pair))
    local.get $choose
    if
      ref.func $ref_target0
      local.set $target
    else
      ref.func $ref_target1
      local.set $target
    end
    local.get $value
    local.get $target
    return_call_ref $ref_pair)

  (func (export "tail_externref_join")
    (param $value i32) (param $choose i32) (result i32 externref)
    (local $target (ref null $extern_pair))
    local.get $choose
    if
      ref.func $extern_target0
      local.set $target
    else
      ref.func $extern_target1
      local.set $target
    end
    local.get $value
    local.get $target
    return_call_ref $extern_pair)

  (func (export "tail_funcref_sret_join")
    (param $value i32) (param $choose i32)
    (result i32 i32 i32 i32 funcref)
    (local $target (ref null $wide_ref_pair))
    local.get $choose
    if
      ref.func $wide_ref_target0
      local.set $target
    else
      ref.func $wide_ref_target1
      local.set $target
    end
    local.get $value
    local.get $target
    return_call_ref $wide_ref_pair))
