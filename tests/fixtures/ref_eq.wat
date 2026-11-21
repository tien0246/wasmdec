;; The local WABT 1.0.42 lacks `eq`/`i31` heap names; the paired .wasm is encoded from this module.
(module
  (func (export "ref_eq") (result i32)
    (ref.null eq)
    (ref.null i31)
    (ref.eq)))
