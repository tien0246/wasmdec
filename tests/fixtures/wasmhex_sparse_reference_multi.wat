(module
  (type $ref_pair (func (param i32) (result i32 funcref)))
  (type $extern_pair (func (param i32) (result i32 externref)))
  (type $i32_fn (func (param i32) (result i32)))
  (table $targets 8 8 funcref)
  (table $extern_targets 8 8 funcref)

  (func $handler0 (export "handler0") (type $i32_fn) local.get 0)
  (func $handler1 (export "handler1") (type $i32_fn)
    local.get 0 i32.const 1 i32.add)
  (func $ref_target0 (export "ref_target0") (type $ref_pair)
    local.get 0
    ref.func $handler0)
  (func $ref_target1 (export "ref_target1") (type $ref_pair)
    local.get 0 i32.const 1 i32.add
    ref.func $handler1)
  (func $extern_target0 (export "extern_target0") (type $extern_pair)
    local.get 0
    ref.null extern)
  (func $extern_target1 (export "extern_target1") (type $extern_pair)
    local.get 0 i32.const 1 i32.add
    ref.null extern)
  (func $indirect_target0 (export "indirect_target0") (type $ref_pair)
    local.get 0
    ref.func $handler0)
  (func $indirect_target1 (export "indirect_target1") (type $ref_pair)
    local.get 0 i32.const 1 i32.add
    ref.func $handler1)
  (func $wrong_target (export "wrong_target") (type $i32_fn)
    local.get 0)

  (elem declare func $handler0 $handler1 $ref_target0 $ref_target1
                       $extern_target0 $extern_target1
                       $indirect_target0 $indirect_target1 $wrong_target)
  (elem (table $targets) (i32.const 0) $indirect_target0)
  (elem (table $targets) (i32.const 5) $wrong_target)
  (elem (table $targets) (i32.const 7) $indirect_target1)
  (elem (table $extern_targets) (i32.const 1) $extern_target0)
  (elem (table $extern_targets) (i32.const 3) $wrong_target)
  (elem (table $extern_targets) (i32.const 6) $extern_target1)

  (func (export "call_ref_funcref_direct") (param $value i32)
    (result i32 funcref)
    local.get $value
    ref.func $ref_target0
    call_ref $ref_pair)

  (func (export "call_ref_funcref_join") (param $value i32) (param $choose i32)
    (result i32 funcref)
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
    call_ref $ref_pair)

  (func (export "call_ref_externref_join")
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
    call_ref $extern_pair)

  (func (export "call_indirect_funcref_sparse")
    (param $index i32) (param $value i32) (result i32 funcref)
    local.get $value
    local.get $index
    call_indirect $targets (type $ref_pair))

  (func (export "call_indirect_externref_sparse")
    (param $index i32) (param $value i32) (result i32 externref)
    local.get $value
    local.get $index
    call_indirect $extern_targets (type $extern_pair))

  (func (export "return_call_indirect_funcref_sparse")
    (param $index i32) (param $value i32) (result i32 funcref)
    local.get $value
    local.get $index
    return_call_indirect $targets (type $ref_pair))

  (func (export "return_call_indirect_externref_sparse")
    (param $index i32) (param $value i32) (result i32 externref)
    local.get $value
    local.get $index
    return_call_indirect $extern_targets (type $extern_pair)))
