(module
  (type $empty (func))
  (tag $signal (type $empty))
  (func (export "trap_not_throw") (result i32)
    (try
      (do (unreachable))
      (catch $signal
        (i32.const 1)
        (return)))
    unreachable))
