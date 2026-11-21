;; The local WABT 1.0.42 cannot assemble function subtype declarations; the paired .wasm is encoded from this module.
(module
  (type $base (sub (func (param i32) (result i32))))
  (type $child (sub $base (func (param i32) (result i32))))
  (func $target (type $child) (param i32) (result i32)
    local.get 0)
  (elem declare func $target)
  (func (export "dispatch") (type $base) (param i32) (result i32)
    local.get 0
    ref.func $target
    call_ref $base))
