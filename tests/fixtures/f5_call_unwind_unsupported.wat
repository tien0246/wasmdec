(module
  (type $empty_type (func))
  (tag $signal (type $empty_type))

  (func $conditional (param i32)
    local.get 0
    if
      throw $signal
    end)

  (func $loop_or_throw (param i32)
    local.get 0
    if
      loop
        br 0
      end
    else
      throw $signal
    end)

  (func (export "caller") (result i32)
    (try
      (do
        (i32.const 1)
        (call $conditional)
        (i32.const 2)
        (return))
      (catch $signal
        (i32.const 1)
        (return)))
    unreachable)

  (func (export "loop_caller") (result i32)
    (try
      (do
        (i32.const 1)
        (call $loop_or_throw)
        (i32.const 2)
        (return))
      (catch $signal
        (i32.const 1)
        (return)))
    unreachable))
