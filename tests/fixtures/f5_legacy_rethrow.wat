(module
  (type $payload_type (func (param i32)))
  (tag $payload (type $payload_type))

  (func (export "rethrow_payload") (result i32) (local i32)
    (try
      (do
        (try
          (do
            (i32.const 42)
            (throw $payload))
          (catch $payload
            (local.set 0)
            (i32.const 99)
            (rethrow 0)))
        (unreachable))
      (catch $payload
        (return)))
    (unreachable)))
