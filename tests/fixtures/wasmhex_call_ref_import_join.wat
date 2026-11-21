(module
  (type $unary_i32 (func (param i32) (result i32)))
  (import "env" "identity" (func $imported (type $unary_i32)))
  (func $defined (type $unary_i32)
    local.get 0
    i32.const 3
    i32.add)
  (export "defined" (func $defined))
  (elem declare func $imported $defined)

  (func (export "dispatch_import_join") (param $value i32) (param $choose i32)
      (result i32)
    (local $target (ref null $unary_i32))
    local.get $choose
    if
      ref.func $imported
      local.set $target
    else
      ref.func $defined
      local.set $target
    end
    local.get $value
    local.get $target
    call_ref $unary_i32)

  (func (export "dispatch_import_tail_join") (param $value i32) (param $choose i32)
      (result i32)
    (local $target (ref null $unary_i32))
    local.get $choose
    if
      ref.func $imported
      local.set $target
    else
      ref.func $defined
      local.set $target
    end
    local.get $value
    local.get $target
    return_call_ref $unary_i32))
