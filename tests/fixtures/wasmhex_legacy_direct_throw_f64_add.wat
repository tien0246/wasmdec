(module
  (type $payload_f64 (func (param f64)))
  (tag $float (type $payload_f64))
  (func (export "computed_f64") (result f64)
    (try
      (do
        (f64.const 1.0)
        (f64.const 2.0)
        (f64.add)
        (throw $float))
      (catch $float
        (return)))
    unreachable))
