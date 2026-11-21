(module
  (type $payload_f32 (func (param f32)))
  (tag $float (type $payload_f32))
  (func (export "local_f32") (param f32) (result f32)
    (try
      (do
        (local.get 0)
        (throw $float))
      (catch $float
        (return)))
    unreachable))
