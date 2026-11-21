(module
  (type $payload_i32 (func (param i32)))
  (tag $signal (type $payload_i32))
  (func (export "nested") (result i32)
    (try
      (do
        (try
          (do
            (i32.const 1)
            (throw $signal))
          (catch $signal
            (i32.const 2)
            (return))))
      (catch $signal
        (i32.const 3)
        (return)))
    unreachable))
