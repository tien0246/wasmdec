;; The local WABT 1.0.42 cannot assemble abstract GC heap names; the paired .wasm is encoded from this module.
(module
  (func (export "cast_nullable") (result i32)
    (ref.null eq)
    (ref.cast (ref null eq))
    drop
    i32.const 1)
  (func (export "cast_nonnull_traps") (result i32)
    (ref.null eq)
    (ref.cast (ref eq))
    drop
    i32.const 1))
