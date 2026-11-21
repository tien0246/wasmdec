(module
  (type $payload_type (func (param i32)))
  (tag $payload (type $payload_type))

  (func (export "trap_in_try") (result i32)
    (try
      (do unreachable)
      (catch $payload
        (drop)
        (i32.const 7)
        (return)))
    unreachable)

  (func (export "throw_in_try") (result i32)
    (try
      (do
        (i32.const 42)
        (throw $payload))
      (catch $payload
        (drop)
        (i32.const 7)
        (return)))
    unreachable))
