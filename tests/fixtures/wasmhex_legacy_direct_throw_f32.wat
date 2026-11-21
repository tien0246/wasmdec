(module
  (type $payload_f32 (func (param f32)))
  (tag $float (type $payload_f32))
  (func (export "payload_f32") (result f32)
    (try
      (do
        (f32.const 1.5)
        (throw $float))
      (catch $float
        (return)))
    unreachable))
