(module
  (type $payload_type (func (param i32)))
  (tag $payload (type $payload_type))

  (func (export "throw_tag") (result i32)
    (try
      (do
        (i32.const 42)
        (throw $payload))
      (catch $payload
        (i32.const 1)
        (i32.add)
        (return)))
    unreachable)

  (func (export "catch_all") (result i32)
    (try
      (do
        (i32.const 90)
        (throw $payload))
      (catch_all
        (i32.const 7)
        (return)))
    unreachable)

  (func (export "normal_skip") (result i32)
    (try
      (do
        (nop))
      (catch $payload
        (drop)))
    (i32.const 3))

  (func (export "delegate_to_outer") (param i32) (result i32)
    (try
      (do
        (try
          (do
            (if (local.get 0)
              (then
                (i32.const 37)
                (throw $payload))))
          (delegate 0))
        (i32.const 99)
        (return))
      (catch $payload
        (return)))
    unreachable))
