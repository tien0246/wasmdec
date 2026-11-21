(module
  (type $a_type (func (param i32)))
  (type $b_type (func (param i64)))
  (type $c_type (func (param f64)))
  (tag $a (type $a_type))
  (tag $b (type $b_type))
  (tag $c (type $c_type))

  (func (export "rethrow_two")
    (try
      (do
        (try
          (do
            (i32.const 42)
            (throw $a))
          (catch $a
            (drop)
            (try
              (do
                (i64.const 99)
                (throw $b))
              (catch $b
                (drop)
                (try
                  (do
                    (f64.const 1)
                    (throw $c))
                  (catch $c
                    (rethrow 2))))))))
      (catch $a
        (drop))))

  (func (export "trap_stays_trap")
    (unreachable)))
