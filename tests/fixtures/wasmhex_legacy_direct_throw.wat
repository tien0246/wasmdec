(module
  (type $empty (func))
  (type $payload_i32 (func (param i32)))
  (type $payload_i64 (func (param i64)))
  (type $payload_f32 (func (param f32)))
  (type $payload_f64 (func (param f64)))
  (tag $empty (type $empty))
  (tag $payload (type $payload_i32))
  (tag $wide (type $payload_i64))
  (tag $float32 (type $payload_f32))
  (tag $float64 (type $payload_f64))

  (func (export "caught_empty") (result i32)
    (try
      (do (throw $empty))
      (catch $empty
        (i32.const 17)
        (return)))
    unreachable)

  (func (export "caught_i32_const") (result i32)
    (try
      (do
        (i32.const 42)
        (throw $payload))
      (catch $payload
        (i32.const 1)
        (i32.add)
        (return)))
    unreachable)

  (func (export "caught_i32_local") (param i32) (result i32)
    (try
      (do
        (local.get 0)
        (throw $payload))
      (catch $payload
        (return)))
    unreachable)

  (func (export "caught_i64_const") (result i64)
    (try
      (do
        (i64.const -81985529216486896)
        (throw $wide))
      (catch $wide
        (return)))
    unreachable)

  (func (export "caught_f32_const") (result f32)
    (try
      (do
        (f32.const 0x1.000002p+0)
        (throw $float32))
      (catch $float32
        (return)))
    unreachable)

  (func (export "caught_f64_const") (result f64)
    (try
      (do
        (f64.const 0x1.0000000000001p+0)
        (throw $float64))
      (catch $float64
        (return)))
    unreachable))
