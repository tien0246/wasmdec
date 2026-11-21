(module
  (type $payload_i32 (func (param i32)))
  (tag $signal (type $payload_i32))
  (func (export "multiple") (result i32)
    (try
      (do
        (i32.const 1)
        (throw $signal)
        (i32.const 2)
        (throw $signal))
      (catch $signal
        (i32.const 3)
        (return)))
    unreachable))
