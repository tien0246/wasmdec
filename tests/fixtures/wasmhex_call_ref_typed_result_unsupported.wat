(module
  (type $item (func (param i32) (result i32)))
  (type $tuple (func (param i32) (result i32 (ref null $item))))
  (import "env" "typed" (func $typed (type $tuple)))
  (elem declare func $typed)
  (func (export "typed_result") (param i32)
    (result i32 (ref null $item))
    local.get 0
    ref.func $typed
    call_ref $tuple))
