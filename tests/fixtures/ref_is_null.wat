;; The local WABT 1.0.42 lacks the `struct` abstract heap name; the paired .wasm is encoded from this module.
(module
  (func (export "is_null_gc") (result i32)
    (ref.null struct)
    (ref.is_null)))
