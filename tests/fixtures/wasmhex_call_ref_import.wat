(module
  (type $identity (func (param i32) (result i32)))
  (import "env" "identity" (func $identity (type $identity)))
  (elem declare func $identity)

  (func (export "call_import") (type $identity)
    local.get 0
    ref.func $identity
    call_ref $identity)

  (func (export "tail_import") (type $identity)
    local.get 0
    ref.func $identity
    return_call_ref $identity))
