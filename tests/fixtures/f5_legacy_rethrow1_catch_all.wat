(module
  (type $a_type (func (param i32)))
  (type $b_type (func (param i64)))
  (tag $a (type $a_type))
  (tag $b (type $b_type))

  (func (export "rethrow1_catch_all") (result i32)
    (try (result i32)
      (do
        (try (result i32)
          (do
            (i32.const 42)
            (throw $a))
          (catch_all
            (try
              (do
                (i64.const 99)
                (throw $b))
              (catch $b
                (rethrow 1)))
            (i32.const 5))))
      (catch $a
        (drop)
        (i32.const 77)))))
