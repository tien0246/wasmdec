;; The local WABT 1.0.42 cannot assemble abstract GC heap names; the paired .wasm is encoded from this module.
(module
  (func (export "test_eq_nullable") (result i32)
    (ref.null eq)
    (ref.test (ref null eq))))
