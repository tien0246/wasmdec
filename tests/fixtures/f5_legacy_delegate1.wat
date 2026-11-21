(module
  (type $payload_type (func (param i32)))
  (tag $signal (type $payload_type))

  (func (export "delegate_one_typed") (param i32) (result i32)
    (try
      (do
        (try
          (do
            (try
              (do
                (if (local.get 0)
                  (then
                    (i32.const 37)
                    (throw $signal))))
              (delegate 1))
            (i32.const 55)
            (drop))
          (delegate 0))
        (i32.const 99)
        (return))
      (catch $signal
        (return)))
    unreachable)

  (func (export "delegate_one_catch_all") (param i32) (result i32)
    (try
      (do
        (try
          (do
            (try
              (do
                (if (local.get 0)
                  (then
                    (i32.const 37)
                    (throw $signal))))
              (delegate 1))
            (nop))
          (delegate 0))
        (i32.const 99)
        (return))
      (catch_all
        (i32.const 7)
        (return)))
    unreachable))
