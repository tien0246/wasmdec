(module
  (type $payload_type (func (param i32)))
  (tag $signal (type $payload_type))

  (func (export "delegate_to_outer") (param i32) (result i32)
    (try
      (do
        (try
          (do
            (if (local.get 0)
              (then
                (i32.const 37)
                (throw $signal))))
          (delegate 0))
        (i32.const 99)
        (return))
      (catch $signal
        (return)))
    unreachable)

  (func (export "delegate_to_function") (param i32)
    (try
      (do
        (if (local.get 0)
          (then
            (i32.const 7)
            (throw $signal))))
      (delegate 0))))
