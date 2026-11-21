(module
  (type $empty_type (func))
  (tag $signal (type $empty_type))

  (func $raise (param i32)
    local.get 0
    drop
    throw $signal)

  (func $trap
    unreachable)

  (func $relay (param i32)
    local.get 0
    call $raise)

  (func (export "caught_call") (result i32)
    (try
      (do
        (i32.const 42)
        (call $relay)
        (i32.const 2)
        (return))
      (catch $signal
        (i32.const 1)
        (return)))
    unreachable)

  (func (export "trap_call") (result i32)
    (try
      (do
        (call $trap)
        (i32.const 2)
        (return))
      (catch $signal
        (i32.const 1)
        (return)))
    unreachable))
