(module
  (type $identity_ref (func (param externref) (result externref)))
  (import "env" "identity" (func $imported (type $identity_ref)))
  (func $defined (type $identity_ref) local.get 0)
  (elem declare func $imported $defined)

  (func (export "dispatch_nonnumeric_import_join")
      (param $value externref) (param $choose i32) (result externref)
    (local $target (ref null $identity_ref))
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
    call_ref $identity_ref))
