(module
  (type $t (func (param i32) (result i32)))
  (import "env" "identity" (func $imported (type $t)))
  (func $target (type $t)
    local.get 0)
  (func $pure_helper
    nop)
  (global $func_ref (mut (ref null $t)) (ref.null $t))
  (table 1 funcref)
  (elem (i32.const 0) func $target)
  (elem declare func $target $imported)
  (func (export "call_import") (type $t)
    local.get 0
    ref.func $imported
    call_ref $t)
  (func (export "tail_import") (type $t)
    local.get 0
    ref.func $imported
    return_call_ref $t)
  (func (export "call_direct") (type $t)
    local.get 0
    ref.func $target
    call_ref $t)
  (func (export "tail_direct") (type $t)
    local.get 0
    ref.func $target
    return_call_ref $t)
  (func (export "call_alias") (param i32) (result i32) (local (ref null $t))
    ref.func $target
    local.set 1
    local.get 0
    local.get 1
    call_ref $t)
  (func (export "tail_alias") (param i32) (result i32) (local (ref null $t))
    ref.func $target
    local.set 1
    local.get 0
    local.get 1
    return_call_ref $t)
  (func (export "call_tee_alias") (param i32) (result i32) (local (ref null $t))
    ref.func $target
    local.tee 1
    drop
    local.get 0
    local.get 1
    call_ref $t)
  (func (export "dispatch_local") (param i32) (result i32) (local i32)
    i32.const 0
    local.set 1
    local.get 0
    local.get 1
    call_indirect (type $t))
  (func (export "dispatch_add") (param i32) (result i32) (local i32)
    i32.const 0
    i32.const 0
    i32.add
    local.set 1
    local.get 0
    local.get 1
    call_indirect (type $t))
  (func (export "call_branch_alias") (param i32 i32) (result i32)
       (local (ref null $t))
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
    call_ref $t)
  (func (export "call_global_alias") (param i32) (result i32)
    ref.func $target
    global.set $func_ref
    local.get 0
    global.get $func_ref
    call_ref $t)
  (func (export "call_global_after_pure_call") (param i32) (result i32)
    ref.func $target
    global.set $func_ref
    call $pure_helper
    local.get 0
    global.get $func_ref
    call_ref $t)
  (func (export "call_global_branch_alias") (param i32 i32) (result i32)
    local.get 1
    if
      ref.func $target
      global.set $func_ref
    else
      ref.func $target
      global.set $func_ref
    end
    local.get 0
    global.get $func_ref
    call_ref $t))
