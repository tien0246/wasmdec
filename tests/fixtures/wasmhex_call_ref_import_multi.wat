(module
  (type $pair (func (param i32) (result i32 i32)))
  (import "env" "pair" (func $pair (type $pair)))
  (elem declare func $pair)

  (func (export "call_import_ref_pair") (param i32) (result i32 i32)
    local.get 0
    ref.func $pair
    call_ref $pair))
