(module
  (type $payload_type (func (param i32)))
  (import "env" "callee" (func $callee))
  (tag $payload (type $payload_type))
  (tag $other (type $payload_type))

  (func (export "rethrow_catch_all")
    (try
      (do
        (try
          (do
            (i32.const 1)
            (throw $payload))
          (catch_all
            (rethrow 0))))
      (catch $payload
        (drop))))

  (func (export "rethrow_unmatched")
    (try
      (do
        (try
          (do
            (i32.const 1)
            (throw $payload))
          (catch $payload
            (rethrow 0))))
      (catch $other
        (drop))))

  (func (export "rethrow_inner_body_call")
    (try
      (do
        (try
          (do
            (call $callee))
          (catch $payload
            (rethrow 0))))
      (catch $payload
        (drop))))

  (func (export "rethrow_outer_body_call")
    (try
      (do
        (call $callee)
        (try
          (do
            (i32.const 1)
            (throw $payload))
          (catch $payload
            (rethrow 0))))
      (catch $payload
        (drop)))))
