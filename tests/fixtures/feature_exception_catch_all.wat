(module
  (func (export "catch_all") (result i32)
    (try (result i32)
      (do (i32.const 90))
      (catch_all (i32.const 7)))))
