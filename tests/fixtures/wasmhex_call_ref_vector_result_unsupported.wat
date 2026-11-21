(module
  (type $tuple (func (param i32) (result i32 v128)))
  (import "env" "vector" (func $vector (type $tuple)))
  (elem declare func $vector)
  (func (export "vector_result") (param i32) (result i32 v128)
    local.get 0
    ref.func $vector
    call_ref $tuple))
