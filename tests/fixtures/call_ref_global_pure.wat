(module
  (type $t (func (param i32) (result i32)))
  (func $target (type $t)
    local.get 0)
  (export "target" (func $target))
  (func $pure
    nop)
  (global $func_ref (mut (ref null $t)) (ref.null $t))
  (func (export "caller") (type $t)
    ref.func $target
    global.set $func_ref
    call $pure
    local.get 0
    global.get $func_ref
    call_ref $t))
