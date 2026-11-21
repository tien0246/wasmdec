;; eqref/i31ref names are not accepted by the local WABT; the paired .wasm is encoded from these bodies.
(module
  (type $references (func (param eqref i31ref) (result i32)))
  (func (export "ref_eq_params") (type $references)
    local.get 0
    local.get 1
    ref.eq)
  (func (export "ref_eq_null") (result i32)
    ref.null eq
    ref.null i31
    ref.eq))
