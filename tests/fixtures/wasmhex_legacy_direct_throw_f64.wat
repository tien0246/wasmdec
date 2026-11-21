(module
  (type $payload_f64 (func (param f64)))
  (tag $float (type $payload_f64))
  (func (export "payload_f64") (result f64)
    (try
      (do
        (f64.const 1.5)
        (throw $float))
      (catch $float
        (return)))
    unreachable))
