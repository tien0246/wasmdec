(module
  (type $t (func (param i32) (result i32)))
  (func $target (type $t)
    local.get 0)
  (export "target" (func $target))
  (func (export "call_ref_dynamic") (param i32 (ref null $t)) (result i32)
    local.get 0
    local.get 1
    call_ref $t))
